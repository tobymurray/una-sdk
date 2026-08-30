#!/usr/bin/env python3
"""M2 — chunk_len sweep to determine round-trip-bound vs bandwidth-bound. Read-only.

Usage: python3 bench_m2.py <device-address> <file-path> [chunk_lens comma-separated]
Appends one JSON line per (chunk_len, run) to raw/m2_chunk_sweep.jsonl.
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

HERE = os.path.dirname(os.path.abspath(__file__))
RAW_LOG = os.path.join(HERE, "raw", "m2_chunk_sweep.jsonl")


async def main():
    address, filepath = sys.argv[1], sys.argv[2]
    chunk_lens = [int(x) for x in sys.argv[3].split(",")] if len(sys.argv) > 3 else [128, 244, 512, 4096]

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    os.makedirs(os.path.dirname(RAW_LOG), exist_ok=True)
    for chunk_len in chunk_lens:
        mtu_out = {}
        t0 = time.monotonic()
        data = await read_file(bus, char_path, filepath, chunk_len=chunk_len, mtu_out=mtu_out)
        elapsed = time.monotonic() - t0
        if data is None:
            print(f"chunk_len={chunk_len}: FAILED (no data)")
            rec = {"measurement": "M2", "chunk_len": chunk_len, "mtu": mtu_out.get("mtu"),
                   "ok": False}
        else:
            bps = len(data) / elapsed
            n_requests = -(-len(data) // chunk_len)  # ceil
            rec = {
                "measurement": "M2", "chunk_len": chunk_len, "mtu": mtu_out.get("mtu"),
                "ok": True, "bytes": len(data), "elapsed_s": elapsed, "bytes_per_sec": bps,
                "n_requests": n_requests, "s_per_request": elapsed / n_requests,
            }
            print(f"chunk_len={chunk_len:5d}  mtu={mtu_out.get('mtu')}  {len(data)} bytes in "
                  f"{elapsed:.3f}s = {bps:.1f} B/s  ({n_requests} requests, "
                  f"{elapsed/n_requests*1000:.1f} ms/req)")
        with open(RAW_LOG, "a") as f:
            f.write(json.dumps(rec) + "\n")
        await asyncio.sleep(1.0)


if __name__ == "__main__":
    asyncio.run(main())
