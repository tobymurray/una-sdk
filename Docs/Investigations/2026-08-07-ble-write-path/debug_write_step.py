#!/usr/bin/env python3
"""Step-by-step write probe with full raw hex dumps at every stage: 0x20 start, then exactly
one 0x22 data packet, dumping the request and response bytes for each."""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402


async def main():
    address, filepath = sys.argv[1], sys.argv[2]
    data = b"hello world, this is a ble write test payload!!"  # 49 bytes, arbitrary

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    path_b = filepath.encode("ascii")
    total_length = len(data)
    async with NotifyStream(bus, char_path) as stream:
        print(f"mtu={stream.mtu}")

        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
                     + struct.pack("<Q", 0) + struct.pack("<I", total_length) + path_b)
        print(f"--> 0x20 start: {start_cmd.hex()}")
        await write_command(bus, char_path, start_cmd)
        b = await stream.get(timeout=8.0)
        print(f"<-- response: {b.hex()}")
        _, status, current_offset, reserved, free_space = struct.unpack("<BBxxIQI", b[:20])
        print(f"    status=0x{status:02x} current_offset={current_offset} free_space={free_space}")

        chunk = data[:min(free_space, total_length)]
        data_cmd = (b"\x22" + bytes([status]) + b"\x00\x00"
                    + struct.pack("<I", current_offset) + struct.pack("<I", free_space)
                    + chunk)
        print(f"--> 0x22 data ({len(chunk)} bytes payload): {data_cmd.hex()}")
        await write_command(bus, char_path, data_cmd)
        try:
            b2 = await stream.get(timeout=8.0)
            print(f"<-- response: {b2.hex()}")
            if len(b2) >= 20:
                c2, s2, o2, r2, f2 = struct.unpack("<BBxxIQI", b2[:20])
                print(f"    cmd=0x{c2:02x} status=0x{s2:02x} current_offset={o2} free_space={f2}")
            elif len(b2) >= 2:
                print(f"    short reply: cmd=0x{b2[0]:02x} val=0x{b2[1]:02x}")
        except asyncio.TimeoutError:
            print("<-- TIMEOUT waiting for response to 0x22")


if __name__ == "__main__":
    asyncio.run(main())
