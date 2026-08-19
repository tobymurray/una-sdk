#!/usr/bin/env python3
"""Validate issue #272 on fw 1.4.0.

Reference read: chunk_len=128 via the validated una_ble_client.read_file (the size the phone
app uses, safely under the old ceiling -- known-good on 1.3.0 and 1.4.0 alike).

Streaming read: ONE 0x10 request at a large chunk_len, then consume framed 0x11 notifications
until offset+real_chunklen == total. Each continuation notification carries its own 16-byte
header on 1.4.0, so this reassembles by declared offset rather than by arrival order.

Asserts: every notification's real_chunklen equals its actual payload length (the #272 bug),
and the reassembled bytes match the reference SHA-256 exactly.
"""
import asyncio, hashlib, os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..",
                                "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream, read_file
from dbus_fast import BusType
from dbus_fast.aio import MessageBus

ADDR, FILEPATH, CHUNK = sys.argv[1], sys.argv[2], int(sys.argv[3])


async def stream_read(bus, char_path, filepath, chunk_len):
    """One 0x10, then consume framed notifications. Returns (bytes, notif_log, mismatches)."""
    path_b = filepath.encode("ascii")
    chunks, total, log, mismatches = {}, None, [], []

    async with NotifyStream(bus, char_path) as stream:
        cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
               + struct.pack("<I", chunk_len) + path_b)
        await write_command(bus, char_path, cmd)
        while True:
            try:
                b = await stream.get(timeout=8.0)
            except asyncio.TimeoutError:
                log.append("TIMEOUT")
                break
            if not b or b[0] != 0x11 or len(b) < 16:
                log.append(f"unexpected frame: {b.hex()}")
                break
            off, total, rcl = struct.unpack("<III", b[4:16])
            payload = b[16:]
            log.append((off, rcl, len(payload)))
            if rcl != len(payload):
                mismatches.append((off, rcl, len(payload)))
            chunks[off] = payload
            if off + len(payload) >= min(total, chunk_len):
                break

    if total is None:
        return None, log, mismatches
    got = sum(len(p) for p in chunks.values())
    buf = bytearray(got)
    for off, p in chunks.items():
        buf[off:off + len(p)] = p
    return bytes(buf), log, mismatches


async def main():
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, ADDR)

    ref = await read_file(bus, char_path, FILEPATH, chunk_len=128)
    print(f"reference (chunk_len=128): {len(ref)} bytes  sha256={hashlib.sha256(ref).hexdigest()}")

    data, log, mismatches = await stream_read(bus, char_path, FILEPATH, CHUNK)
    print(f"\nstreamed  (chunk_len={CHUNK}): {len(data)} bytes  sha256={hashlib.sha256(data).hexdigest()}")
    print(f"notifications: {len(log)}")
    for entry in log:
        if isinstance(entry, tuple):
            off, rcl, n = entry
            flag = "  <-- MISMATCH" if rcl != n else ""
            print(f"    offset={off:<6} real_chunklen={rcl:<4} payload={n:<4}{flag}")
        else:
            print(f"    {entry}")

    print()
    print(f"header-accounting mismatches: {len(mismatches)}  {'FAIL' if mismatches else 'PASS'}")
    print(f"bytes identical to reference: {'PASS' if data == ref else 'FAIL'}")

asyncio.run(main())
