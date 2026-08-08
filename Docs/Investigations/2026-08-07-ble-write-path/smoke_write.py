#!/usr/bin/env python3
"""M4 smoke test: write a small known payload to an allowlisted scratch path, read it back,
and byte-compare. First real exercise of write_file()'s full 0x20/0x21/0x22 loop."""
import asyncio
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_file, read_file  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402


async def main():
    address, filepath = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 126
    unit = f"ble write smoke test @ {time.time()}\n".encode("ascii")
    payload = (unit * (size // len(unit) + 1))[:size]

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    print(f"writing {len(payload)} bytes to {filepath} ...")
    t0 = time.monotonic()

    def progress(sent, total):
        print(f"  progress: {sent}/{total} ({time.monotonic()-t0:.3f}s elapsed)")

    final_offset = await write_file(bus, char_path, filepath, payload, progress=progress)
    print(f"write_file() returned final_offset={final_offset} in {time.monotonic()-t0:.3f}s")

    print("reading back ...")
    readback = await read_file(bus, char_path, filepath, chunk_len=128)
    if readback is None:
        print("READBACK FAILED (file not found / no data)")
        sys.exit(1)
    print(f"read back {len(readback)} bytes")
    if readback == payload:
        print("MATCH: byte-exact round trip")
    else:
        print("MISMATCH!")
        print(f"  expected: {payload!r}")
        print(f"  got:      {readback!r}")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
