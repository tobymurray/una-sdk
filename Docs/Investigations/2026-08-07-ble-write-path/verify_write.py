#!/usr/bin/env python3
"""Read back an M5-written file and byte-compare against the deterministic payload
make_payload(size) would produce, as a separate step from the write itself (so each half fits
comfortably under a single command's time budget for large sizes).

Usage: python3 verify_write.py <device-address> <size-bytes> [path-suffix] [chunk_len]
Appends a JSON record to raw/m5_verify.jsonl.
"""
import asyncio
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, read_file  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402
from bench_m5 import make_payload  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RAW_LOG = os.path.join(HERE, "raw", "m5_verify.jsonl")


async def main():
    address, size = sys.argv[1], int(sys.argv[2])
    suffix = sys.argv[3] if len(sys.argv) > 3 else str(size)
    chunk_len = int(sys.argv[4]) if len(sys.argv) > 4 else 201
    filepath = f"/Apps/HelloWorld/ble_bench_m5_{suffix}.bin"
    expected = make_payload(size)

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    print(f"reading back {filepath} ...")
    t0 = time.monotonic()
    readback = await read_file(bus, char_path, filepath, chunk_len=chunk_len)
    elapsed = time.monotonic() - t0

    rec = {"measurement": "M5-verify", "size": size, "path": filepath, "read_elapsed_s": elapsed}
    if readback is None:
        rec["ok"] = False
        print("READBACK FAILED")
    else:
        rec["readback_len"] = len(readback)
        rec["read_bytes_per_sec"] = len(readback) / elapsed if elapsed else None
        rec["ok"] = (readback == expected)
        print(f"readback {len(readback)} bytes in {elapsed:.3f}s "
              f"({len(readback)/elapsed:.1f} B/s) -- {'MATCH' if rec['ok'] else 'MISMATCH'}")

    os.makedirs(os.path.dirname(RAW_LOG), exist_ok=True)
    with open(RAW_LOG, "a") as f:
        f.write(json.dumps(rec) + "\n")


if __name__ == "__main__":
    asyncio.run(main())
