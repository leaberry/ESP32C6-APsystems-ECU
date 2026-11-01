"""Convert an aps-passive-v1 CSV to Wireshark IEEE 802.15.4 (no FCS) PCAP."""
import argparse
import csv
from pathlib import Path
import struct


def convert(source, destination):
    lines = Path(source).read_text(encoding="utf-8-sig").splitlines()
    if not lines or not lines[0].startswith("# aps-passive-v1 "):
        raise ValueError("Expected an aps-passive-v1 capture")
    result = bytearray(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 127, 230))
    count = 0
    previous = None
    wraps = 0
    for row in csv.DictReader(line for line in lines if not line.startswith("#")):
        raw = bytes.fromhex(row["raw_hex"])
        if len(raw) < 4 or len(raw) != raw[0] + 1 or raw[0] > 127:
            raise ValueError(f"Invalid PHY length at packet {count + 1}")
        ms = int(row["uptime_ms"])
        if previous is not None and ms < previous:
            if previous - ms < 0x80000000:
                raise ValueError("Packet timestamps are out of order")
            wraps += 1
        previous = ms
        # Strip the PHY length and trailing driver FCS slots. Preserve MAC/NWK/APS.
        packet = raw[1:-2]
        stamp = ms + wraps * (1 << 32)
        result.extend(struct.pack("<IIII", stamp // 1000, stamp % 1000 * 1000, len(packet), len(packet)))
        result.extend(packet)
        count += 1
    Path(destination).write_bytes(result)
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(f"Converted {convert(args.capture, args.output)} packets; timestamps are board uptime.")
