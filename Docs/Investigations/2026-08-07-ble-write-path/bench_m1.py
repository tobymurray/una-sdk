#!/usr/bin/env python3
"""M1 — baseline read throughput + negotiated MTU. Read-only, no writes to the device.

Usage: python3 bench_m1.py <device-address> <file-path> [runs]
Appends one JSON line per run to raw/m1_read_baseline.jsonl next to this script.
"""
import asyncio
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, read_file, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RAW_LOG = os.path.join(HERE, "raw", "m1_read_baseline.jsonl")


async def main():
    address, filepath = sys.argv[1], sys.argv[2]
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 3

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as s:
        mtu = s.mtu
    print(f"negotiated MTU: {mtu}")

    os.makedirs(os.path.dirname(RAW_LOG), exist_ok=True)
    results = []
    for i in range(runs):
        t0 = time.monotonic()
        data = await read_file(bus, char_path, filepath, chunk_len=128)
        elapsed = time.monotonic() - t0
        if data is None:
            print(f"run {i}: FAILED (no data)")
            continue
        bps = len(data) / elapsed
        rec = {
            "measurement": "M1", "run": i, "file": filepath, "mtu": mtu,
            "chunk_len": 128, "bytes": len(data), "elapsed_s": elapsed,
            "bytes_per_sec": bps,
        }
        results.append(rec)
        with open(RAW_LOG, "a") as f:
            f.write(json.dumps(rec) + "\n")
        print(f"run {i}: {len(data)} bytes in {elapsed:.3f}s = {bps:.1f} B/s")
        await asyncio.sleep(1.0)

    if results:
        rates = [r["bytes_per_sec"] for r in results]
        print(f"\n{len(results)}/{runs} runs OK. min={min(rates):.1f} max={max(rates):.1f} "
              f"mean={sum(rates)/len(rates):.1f} B/s")


if __name__ == "__main__":
    asyncio.run(main())
