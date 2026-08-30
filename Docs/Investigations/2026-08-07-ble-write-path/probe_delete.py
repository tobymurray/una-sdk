#!/usr/bin/env python3
"""Probe the 0x30 DELETE opcode (Adafruit hypothesis: struct.pack('<BxH', 0x30, len(path)) +
path -> response struct.unpack('<BB', buf): cmd, status). This is also the previously
"still-unexplained" 0x30/response-byte pair noted in BLE-COMPANION-protocol-spec.md §2.2 from
the original phone capture -- confirming it doubles as closing that open item.

Usage: python3 probe_delete.py <device-address> <path>
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
    address, path = sys.argv[1], sys.argv[2]
    check_write_path_allowed(path)  # reuse the same allowlist guard -- delete is destructive too
    path_b = path.encode("ascii")

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as stream:
        cmd = b"\x30\x00" + struct.pack("<H", len(path_b)) + path_b
        print(f"sending 0x30 delete: {cmd.hex()}")
        await write_command(bus, char_path, cmd)
        try:
            b = await stream.get(timeout=8.0)
            print(f"response: {len(b)} bytes: {b.hex()}")
        except asyncio.TimeoutError:
            print("TIMEOUT -- no response")


if __name__ == "__main__":
    asyncio.run(main())
