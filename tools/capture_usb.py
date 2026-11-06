"""Save a 15-minute channel-16 recording from the QS1 USB listener."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import time

VERSION = "ESP32C6-ECU_v1_4_20-qs1-usb6"
HEADER = "uptime_ms,channel,rssi_dbm,lqi,raw_hex,sequence"


def fields(line):
    return dict(word.split("=", 1) for word in line.split() if "=" in word)


def check_ready(line):
    values = fields(line)
    if values.get("firmware") != VERSION:
        raise ValueError("Wrong firmware. Install the qs1-usb6 diagnostic build.")
    if any(values.get(k) != v for k, v in {
            "ready": "1", "state": "idle", "channel": "16",
            "wifi": "off", "bluetooth": "off", "fault": "none"}.items()):
        raise ValueError("Listener is not ready: " + line)
    return values


class Recording:
    """Validate the stream while preserving the original packet bytes."""
    def __init__(self):
        self.started = False
        self.columns = False
        self.rows = 0
        self.sequence = 0
        self.missing = 0
        self.previous_ms = None

    def accept(self, line):
        if line.startswith("# aps-passive-v1 "):
            if self.started:
                raise ValueError("Listener restarted the recording unexpectedly")
            values = fields(line)
            expected = {"firmware": VERSION, "transport": "usb", "channel": "16",
                        "wifi": "off", "bluetooth": "off", "promiscuous": "1",
                        "ack_tx": "0", "ack_rx": "0", "enh_ack": "0"}
            if any(values.get(k) != v for k, v in expected.items()):
                raise ValueError("Radio checks failed: " + line)
            self.started = True
        elif line == HEADER:
            if not self.started or self.columns:
                raise ValueError("Unexpected column header")
            self.columns = True
        elif line.startswith("# end "):
            if not self.columns:
                raise ValueError("Recording ended without a header")
            values = fields(line)
            received, written, dropped = (int(values[k]) for k in
                                         ("received", "written", "queue_drops"))
            if written != self.rows or received != written + dropped or received < self.sequence:
                raise ValueError("Packet totals do not match; recording is incomplete")
            if self.missing + received - self.sequence != dropped:
                raise ValueError("Unexplained missing packet numbers")
            if values.get("tx") != "0" or values.get("fault") != "none":
                raise ValueError("Radio reported an unexpected state: " + line)
            return values
        elif line.startswith("# progress "):
            if not self.started:
                raise ValueError("Missing recording header")
        elif line.startswith("#"):
            raise ValueError("Unexpected device message: " + line)
        else:
            if not self.columns:
                raise ValueError("Packet arrived before its header")
            parts = line.split(",")
            if len(parts) != 6:
                raise ValueError("Incomplete packet line")
            ms, channel, rssi, lqi = map(int, parts[:4])
            raw = bytes.fromhex(parts[4])
            sequence = int(parts[5])
            if not (0 <= ms <= 0xffffffff and channel == 16 and
                    -128 <= rssi <= 127 and 0 <= lqi <= 255 and
                    4 <= len(raw) <= 128 and len(raw) == raw[0] + 1):
                raise ValueError("Invalid packet metadata or length")
            if sequence <= self.sequence:
                raise ValueError("Repeated or out-of-order packet number")
            if self.previous_ms is not None and (ms - self.previous_ms) & 0xffffffff > 0x7fffffff:
                raise ValueError("Packet timestamps moved backward")
            self.missing += sequence - self.sequence - 1
            self.sequence = sequence
            self.previous_ms = ms
            self.rows += 1
        return None


class LineReader:
    def __init__(self, port):
        self.port = port
        self.buffer = bytearray()

    def read_line(self):
        # Preserve split lines and consume buffered USB data in batches.
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            newline = self.buffer.find(b"\n")
            if 0 <= newline <= 2048:
                line = self.buffer[:newline].rstrip(b"\r").decode("ascii")
                del self.buffer[:newline + 1]
                return line
            if len(self.buffer) > 2048:
                raise ValueError("Invalid USB output; check the firmware and USB port")
            self.buffer.extend(self.port.read(max(1, min(self.port.in_waiting, 4096))))
        raise TimeoutError("No complete USB message for 12 seconds")


def wait_ready(reader, antenna=None):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        line = reader.read_line()
        if line.startswith("# aps-usb-ready-v1 "):
            values = check_ready(line)
            if antenna is None or values.get("antenna") == antenna:
                return values
    raise TimeoutError("USB listener did not become ready")


def record(port, destination, antenna):
    reader = LineReader(port)
    port.write(b"STATUS\n")
    wait_ready(reader)
    port.write(b"ANTENNA XIAO\n" if antenna == "xiao" else b"ANTENNA SAVED\n")
    status = wait_ready(reader, "xiao-external" if antenna == "xiao" else "saved")
    print("Firmware verified. Wi-Fi and Bluetooth are off.")
    print(f"Antenna: {status['antenna']}; mode {status['antenna_mode']} (0=unmanaged, 1=internal, 2=external).")
    print("Recording channel 16 for 15 minutes. Keep this window open and the computer awake.")
    print("Leave the older controller running. Press Ctrl+C to save a partial recording.")
    state = Recording()
    # Exclusive creation prevents accidentally replacing a previous capture.
    with destination.open("x", encoding="ascii", newline="\n") as output:
        deadline = time.monotonic() + 940
        cancelled = False
        try:
            port.write(b"START\n")
            while time.monotonic() < deadline:
                try:
                    line = reader.read_line()
                except KeyboardInterrupt:
                    if cancelled:
                        raise
                    cancelled = True
                    port.write(b"STOP\n")
                    deadline = time.monotonic() + 30
                    print("Stopping; saving remaining packets...")
                    continue
                if not state.started and line.startswith("# aps-usb-ready-v1 "):
                    check_ready(line)
                    continue
                output.write(line + "\n")
                output.flush()
                end = state.accept(line)
                if line.startswith("# progress "):
                    progress = fields(line)
                    print(f"{int(progress['elapsed_ms']) // 1000:3d}s: {state.rows} packets, "
                          f"{progress['queue_drops']} dropped", end="\r", flush=True)
                if end:
                    complete = end["reason"] == "time-limit" and int(end["elapsed_ms"]) >= 900000
                    output.write(f"# host_complete={int(complete)} host_packets={state.rows}\n")
                    print(f"\n{'Finished' if complete else 'Partial capture'}: {destination.resolve()}")
                    print(f"{state.rows} packets; {end['queue_drops']} queue drops; {end['invalid']} invalid frames.")
                    print("Send this CSV even if it is empty or reports drops.")
                    return complete
            raise TimeoutError("Recording did not finish within the expected time")
        except BaseException:
            output.write(f"# host_complete=0 host_packets={state.rows} reason=interrupted-or-invalid-stream\n")
            try:
                port.write(b"STOP\n")
            except Exception:
                pass
            print(f"\nIncomplete capture saved: {destination.resolve()}")
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB port, for example COM7 or /dev/ttyACM0")
    parser.add_argument("--antenna", choices=("saved", "xiao"), default="saved",
                        help="xiao selects the XIAO external antenna for this boot only")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        parser.exit(1, "Install the USB reader first: py -m pip install pyserial\n")
    name = args.port
    if not name:
        ports = list(list_ports.comports())
        if not ports:
            parser.exit(1, "No USB ports found. Connect the board with a data cable.\n")
        for p in ports:
            print(f"{p.device}: {p.description}")
        name = input("Enter the listener's port (for example COM7): ").strip()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%SZ")
    destination = args.output or Path(f"aps-usb-capture-{stamp}.csv")
    try:
        port = serial.Serial()
        port.port = name
        port.baudrate = 115200
        port.timeout = 1
        port.write_timeout = 2
        port.dtr = False
        port.rts = False
        port.open()
        with port:
            complete = record(port, destination, args.antenna)
        return 0 if complete else 2
    except (OSError, ValueError, TimeoutError, KeyboardInterrupt, serial.SerialException) as error:
        print(f"Capture stopped: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
