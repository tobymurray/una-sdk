#!/usr/bin/env python3
"""Does the EXISTING una_ble_client.read_file still work on 1.4.0 above the old ceiling?"""
import asyncio, hashlib, os, sys
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
    for cl in (128, 201, 512, 4096):
        try:
            d = await asyncio.wait_for(read_file(bus, cp, FILEPATH, chunk_len=cl), timeout=120)
        except asyncio.TimeoutError:
            print(f"chunk_len={cl:<5} -> HUNG (timeout)"); continue
        if d is None:
            print(f"chunk_len={cl:<5} -> None"); continue
        h = hashlib.sha256(d).hexdigest()
        if ref is None:
            ref = h
        print(f"chunk_len={cl:<5} -> {len(d):>6} bytes  sha={h[:16]}  {'match' if h == ref else 'MISMATCH'}")

asyncio.run(main())
