#!/usr/bin/env python3
"""M4 safe probe: send a 0x20 WRITE-start with a deliberately bogus offset to an allowlisted
scratch path. Sends NO data (0x22) -- purely to see whether the device validates offset/framing
the way the Adafruit-shaped hypothesis predicts, before trusting write_file() with real bytes.
"""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream, check_write_path_allowed  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402


async def main():
    address, filepath, offset = sys.argv[1], sys.argv[2], int(sys.argv[3])
    total_length = int(sys.argv[4]) if len(sys.argv) > 4 else 100
    check_write_path_allowed(filepath)  # same guard write_file() uses -- refuse if not allowlisted

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    path_b = filepath.encode("ascii")
    async with NotifyStream(bus, char_path) as stream:
        print(f"mtu={stream.mtu}")
        cmd = (b"\x20\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", offset)
               + struct.pack("<Q", 0) + struct.pack("<I", total_length) + path_b)
        print(f"sending 0x20 start-write: path={filepath!r} offset={offset} total_length={total_length}")
        print(f"raw request: {cmd.hex()}")
        await write_command(bus, char_path, cmd)
        try:
            b = await stream.get(timeout=8.0)
            print(f"response: {len(b)} bytes: {b.hex()}")
            if len(b) >= 20:
                cmd_echo, status, current_offset, reserved, free_space = struct.unpack("<BBxxIQI", b[:20])
                print(f"  parsed (Adafruit WRITE_PACING shape): cmd=0x{cmd_echo:02x} "
                      f"status=0x{status:02x} current_offset={current_offset} "
                      f"reserved={reserved} free_space={free_space}")
            elif len(b) >= 2:
                print(f"  short response, first two bytes: cmd=0x{b[0]:02x} val=0x{b[1]:02x}")
        except asyncio.TimeoutError:
            print("TIMEOUT -- no response at all to the bogus-offset write-start")


if __name__ == "__main__":
    asyncio.run(main())
