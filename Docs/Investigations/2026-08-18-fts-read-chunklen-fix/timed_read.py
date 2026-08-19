#!/usr/bin/env python3
"""Time the updated read_file() on a large file at several chunk sizes; all must agree."""
import asyncio, hashlib, os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..",
                                "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, read_file
from dbus_fast import BusType
from dbus_fast.aio import MessageBus

ADDR, FILEPATH = sys.argv[1], sys.argv[2]

async def main():
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    cp = await find_fts_characteristic(bus, ADDR)
    ref = None
    for cl in (int(a) for a in sys.argv[3:]):
        t0 = time.time()
        try:
            d = await asyncio.wait_for(read_file(bus, cp, FILEPATH, chunk_len=cl), timeout=600)
        except asyncio.TimeoutError:
            print(f"chunk_len={cl:<5} -> HUNG"); continue
        dt = time.time() - t0
        h = hashlib.sha256(d).hexdigest()
        ref = ref or h
        print(f"chunk_len={cl:<5} -> {len(d):>7} bytes in {dt:6.1f}s "
              f"({len(d)/dt/1024:5.1f} kB/s)  sha={h[:16]}  {'match' if h == ref else 'MISMATCH'}")

asyncio.run(main())
