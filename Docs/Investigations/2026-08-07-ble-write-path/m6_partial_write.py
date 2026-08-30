#!/usr/bin/env python3
"""M6 -- what happens when a write goes wrong. Starts a multi-chunk write, deliberately stops
partway through (simulating a client crash / connection loss) by closing the notify session
without finishing, then a separate step re-reads the file to see what state it's left in and
tries to resume.

Usage:
    python3 m6_partial_write.py start <device-address> <total-size> <stop-after-n-chunks>
    python3 m6_partial_write.py readback <device-address>
    python3 m6_partial_write.py resume <device-address> <total-size>
"""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, read_file, NotifyStream, check_write_path_allowed  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402
from bench_m5 import make_payload  # noqa: E402

FILEPATH = "/Apps/HelloWorld/ble_bench_m6_wrongoffset.bin"


async def start_partial(address, total_size, stop_after_n):
    check_write_path_allowed(FILEPATH)
    data = make_payload(total_size)
    path_b = FILEPATH.encode("ascii")

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as stream:
        max_att_chunk = stream.mtu - 3 - 12
        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
                     + struct.pack("<Q", 0) + struct.pack("<I", total_size) + path_b)
        await write_command(bus, char_path, start_cmd)
        b = await stream.get(timeout=8.0)
        _, status, _off, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
        print(f"start: status=0x{status:02x} free_space={free_space}")

        bytes_sent = 0
        for i in range(stop_after_n):
            n = min(free_space, total_size - bytes_sent, max_att_chunk)
            if n <= 0 or bytes_sent >= total_size:
                break
            chunk = data[bytes_sent:bytes_sent + n]
            data_cmd = (b"\x22" + bytes([status]) + b"\x00\x00"
                        + struct.pack("<I", bytes_sent) + struct.pack("<I", len(chunk)) + chunk)
            await asyncio.wait_for(write_command(bus, char_path, data_cmd), timeout=10.0)
            b = await stream.get(timeout=8.0)
            _, status, _off, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
            bytes_sent += n
            print(f"  chunk {i}: sent {n} bytes, bytes_sent={bytes_sent}, status=0x{status:02x}")

    print(f"STOPPING deliberately after {bytes_sent}/{total_size} bytes -- "
          f"closing notify session without finishing the write, no further 0x22 sent.")
    print(f"bytes_sent={bytes_sent}")


async def readback(address):
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)
    data = await read_file(bus, char_path, FILEPATH, chunk_len=201)
    if data is None:
        print("READ FAILED: file not found or no response")
    else:
        print(f"read back {len(data)} bytes")
        print(f"first 32 bytes: {data[:32].hex()}")
        print(f"last 32 bytes:  {data[-32:].hex()}")
        nonzero_tail = any(b != 0 for b in data[-32:])
        print(f"trailing bytes nonzero: {nonzero_tail}")


async def resume(address, total_size):
    """Try to resume the aborted write at a nonzero offset, matching what start_partial()
    reported as bytes_sent -- to see whether the firmware accepts continuing a previously
    interrupted write or insists on starting over."""
    resume_offset = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    path_b = FILEPATH.encode("ascii")
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as stream:
        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b))
                     + struct.pack("<I", resume_offset) + struct.pack("<Q", 0)
                     + struct.pack("<I", total_size) + path_b)
        print(f"sending 0x20 start with offset={resume_offset} (attempting resume)")
        await write_command(bus, char_path, start_cmd)
        b = await stream.get(timeout=8.0)
        print(f"response: {len(b)} bytes: {b.hex()}")
        if len(b) >= 20:
            _, status, off, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
            print(f"  status=0x{status:02x} offset={off} free_space={free_space}")
        elif len(b) >= 2:
            print(f"  short reply: cmd=0x{b[0]:02x} val=0x{b[1]:02x} "
                  f"(likely error status per Adafruit taxonomy)")


async def finish_resume(address, total_size, resume_offset):
    """Resume an interrupted write from resume_offset through to total_size, then confirm the
    result byte-exact-matches the deterministic payload."""
    data = make_payload(total_size)
    path_b = FILEPATH.encode("ascii")
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    async with NotifyStream(bus, char_path) as stream:
        max_att_chunk = stream.mtu - 3 - 12
        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b))
                     + struct.pack("<I", resume_offset) + struct.pack("<Q", 0)
                     + struct.pack("<I", total_size) + path_b)
        await write_command(bus, char_path, start_cmd)
        b = await stream.get(timeout=8.0)
        _, status, device_offset, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
        print(f"resume start: status=0x{status:02x} device_offset={device_offset} "
              f"free_space={free_space}")
        if status != 0x01:
            print("resume rejected, aborting")
            return

        bytes_sent = resume_offset
        while bytes_sent < total_size:
            n = min(free_space, total_size - bytes_sent, max_att_chunk)
            chunk = data[bytes_sent:bytes_sent + n]
            data_cmd = (b"\x22" + bytes([status]) + b"\x00\x00"
                        + struct.pack("<I", bytes_sent) + struct.pack("<I", len(chunk)) + chunk)
            await asyncio.wait_for(write_command(bus, char_path, data_cmd), timeout=10.0)
            b = await stream.get(timeout=8.0)
            _, status, _off, _res, free_space = struct.unpack("<BBxxIQI", b[:20])
            bytes_sent += n
            print(f"  sent up to {bytes_sent}/{total_size}, status=0x{status:02x}")

    print(f"resume complete, final bytes_sent={bytes_sent}")

    readback_data = await read_file(bus, char_path, FILEPATH, chunk_len=201)
    if readback_data is None:
        print("READBACK FAILED")
    elif readback_data == data:
        print(f"MATCH: byte-exact {len(readback_data)}-byte file after resume")
    else:
        print(f"MISMATCH: readback {len(readback_data)} bytes, expected {len(data)}")
        for i in range(min(len(readback_data), len(data))):
            if readback_data[i] != data[i]:
                print(f"  first differing byte at offset {i}: "
                      f"got 0x{readback_data[i]:02x} expected 0x{data[i]:02x}")
                break


async def main():
    mode = sys.argv[1]
    address = sys.argv[2]
    if mode == "start":
        await start_partial(address, int(sys.argv[3]), int(sys.argv[4]))
    elif mode == "readback":
        await readback(address)
    elif mode == "resume":
        await resume(address, int(sys.argv[3]))
    elif mode == "finish_resume":
        await finish_resume(address, int(sys.argv[3]), int(sys.argv[4]))
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
