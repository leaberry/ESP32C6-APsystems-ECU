"""Convert a passive or QS1 pairing CSV to Wireshark IEEE 802.15.4 (no FCS) PCAP."""
import argparse
import csv
from pathlib import Path
import struct
import re


def convert(source, destination):
    lines = Path(source).read_text(encoding="utf-8-sig").splitlines()
    active = bool(lines and lines[0].startswith("# qs1-active-v1 "))
    if not lines or not (active or lines[0].startswith("# aps-passive-v1 ")):
        raise ValueError("Expected a passive or QS1 pairing capture")
    result = bytearray(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 127, 230))
    count = 0
    previous = None
    wraps = 0
    rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
    if active:
        # RX can be logged before a blocking TX finishes. Sort by receive/send time.
        match = re.search(r'\bstarted=(\d+)', lines[0])
        if not match:
            raise ValueError("QS1 capture start time missing")
        start = int(match[1])
        rows = [r for r in rows if r['direction'] in ('R', 'T')]
        rows.sort(key=lambda r: (int(r['uptime_ms']) - start) & 0xFFFFFFFF)
    for row in rows:
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
