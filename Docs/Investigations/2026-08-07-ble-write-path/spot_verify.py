#!/usr/bin/env python3
"""Spot-check verification for the largest M5 sizes (8/29 MiB), where a full byte-exact
readback would itself take as long as the write (another ~1h / ~4h) and duplicate what the
1 MiB and 3 MiB full readbacks already proved about correctness at this flat rate. Reads a few
single 0x10 requests at specific offsets (first, middle, last) instead of walking the whole
file, and checks the declared total_size against the expected size.

Usage: python3 spot_verify.py <device-address> <size-bytes> [path-suffix]
"""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402
from bench_m5 import make_payload  # noqa: E402


async def read_one_chunk(bus, char_path, filepath, offset, chunk_len=200):
    path_b = filepath.encode("ascii")
    async with NotifyStream(bus, char_path) as stream:
        cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", offset)
               + struct.pack("<I", chunk_len) + path_b)
        await write_command(bus, char_path, cmd)
        b = await stream.get(timeout=8.0)
        if not b or b[0] != 0x11 or len(b) < 16:
            return None, None
        got_offset, total, real_chunklen = struct.unpack("<III", b[4:16])
        payload = bytes(b[16:16 + real_chunklen])
        return total, payload


async def main():
    address, size = sys.argv[1], int(sys.argv[2])
    suffix = sys.argv[3] if len(sys.argv) > 3 else str(size)
    filepath = f"/Apps/HelloWorld/ble_bench_m5_{suffix}.bin"
    expected = make_payload(size)

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    ok = True
    checkpoints = [0, size // 2, max(0, size - 200)]
    for off in checkpoints:
        total, payload = await read_one_chunk(bus, char_path, filepath, off)
        if total is None:
            print(f"offset={off}: NO RESPONSE")
            ok = False
            continue
        expected_chunk = expected[off:off + len(payload)]
        match = payload == expected_chunk
        print(f"offset={off}: total_size={total} (expected {size}) "
              f"payload={len(payload)}B match={match}")
        ok = ok and match and (total == size)

    print(f"\nSPOT VERIFY {'PASSED' if ok else 'FAILED'} for {filepath} ({size} bytes)")


if __name__ == "__main__":
    asyncio.run(main())
