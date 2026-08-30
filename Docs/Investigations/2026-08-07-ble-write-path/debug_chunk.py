#!/usr/bin/env python3
"""Ad-hoc probe: send one 0x10 read request at a given chunk_len and dump whatever comes back."""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402


async def main():
    address, filepath, chunk_len = sys.argv[1], sys.argv[2], int(sys.argv[3])
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    path_b = filepath.encode("ascii")
    async with NotifyStream(bus, char_path) as stream:
        print(f"mtu={stream.mtu}")
        cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
               + struct.pack("<I", chunk_len) + path_b)
        await write_command(bus, char_path, cmd)
        for i in range(6):
            try:
                b = await stream.get(timeout=6.0)
            except asyncio.TimeoutError:
                print(f"[{i}] TIMEOUT waiting for notification")
                break
            print(f"[{i}] {len(b)} bytes: {b.hex()}")
            if b and b[0] == 0x11 and len(b) >= 16:
                got_offset, total, real_chunklen = struct.unpack("<III", b[4:16])
                print(f"    parsed: offset={got_offset} total={total} real_chunklen={real_chunklen} "
                      f"payload_bytes_here={len(b)-16}")


if __name__ == "__main__":
    asyncio.run(main())
