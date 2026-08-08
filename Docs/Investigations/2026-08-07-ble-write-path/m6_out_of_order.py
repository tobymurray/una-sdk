#!/usr/bin/env python3
"""M6: within a single still-open write session (no reconnect, no fresh 0x20), send one valid
chunk at offset 0, then a 0x22 claiming a wildly out-of-order offset -- does the device validate
intra-session offset continuity, or accept whatever offset the client claims?"""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402

FILEPATH = "/Apps/HelloWorld/ble_bench_m6_outoforder.bin"


async def main():
    address = sys.argv[1]
    total_size = 3000
    data = bytes((i % 251) for i in range(total_size))  # simple distinct pattern
    path_b = FILEPATH.encode("ascii")

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as stream:
        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
                     + struct.pack("<Q", 0) + struct.pack("<I", total_size) + path_b)
        await write_command(bus, char_path, start_cmd)
        b = await stream.get(timeout=8.0)
        _, status, _off, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
        print(f"start: status=0x{status:02x} free_space={free_space}")

        # valid first chunk at offset 0
        chunk0 = data[0:205]
        cmd0 = (b"\x22" + bytes([status]) + b"\x00\x00" + struct.pack("<I", 0)
                + struct.pack("<I", len(chunk0)) + chunk0)
        await write_command(bus, char_path, cmd0)
        b = await stream.get(timeout=8.0)
        print(f"chunk@0: response {b.hex()}")

        # now claim offset=1000 (way ahead, skipping 205..1000) with no new 0x20
        bogus_offset = 1000
        chunk1 = data[bogus_offset:bogus_offset + 205]
        cmd1 = (b"\x22" + bytes([0x01]) + b"\x00\x00" + struct.pack("<I", bogus_offset)
                + struct.pack("<I", len(chunk1)) + chunk1)
        print(f"sending out-of-order chunk claiming offset={bogus_offset} "
              "(session only saw 0..205 so far)")
        await write_command(bus, char_path, cmd1)
        try:
            b = await stream.get(timeout=8.0)
            print(f"response: {len(b)} bytes: {b.hex()}")
            if len(b) >= 20:
                _, status2, off2, _r, fs2 = struct.unpack("<BBxxIQI", b[:20])
                print(f"  status=0x{status2:02x} offset={off2} free_space={fs2}")
            elif len(b) >= 2:
                print(f"  short reply: cmd=0x{b[0]:02x} val=0x{b[1]:02x}")
        except asyncio.TimeoutError:
            print("TIMEOUT -- no response to the out-of-order chunk")


if __name__ == "__main__":
    asyncio.run(main())
