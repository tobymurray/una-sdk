#!/usr/bin/env python3
"""Full-file FTS read at a large chunk_len on fw 1.4.0, with independent FIT CRC-16 integrity check.

Issues one 0x10 per chunk (offset advancing by the REQUESTED chunk_len) and consumes the
framed 0x11 notifications the firmware streams back for each. Checks:
  1. every notification's real_chunklen equals its actual payload length   (the #272 bug)
  2. each request delivers exactly min(chunk_len, remaining) bytes         (clamp is gone)
  3. the reassembled file's trailing FIT CRC-16 validates                  (bytes are correct)
"""
import asyncio, hashlib, os, struct, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..",
                                "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream
from dbus_fast import BusType
from dbus_fast.aio import MessageBus

ADDR, FILEPATH, CHUNK = sys.argv[1], sys.argv[2], int(sys.argv[3])

_CRC = [0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400]


def fit_crc(data: bytes) -> int:
    """FIT protocol CRC-16 (nibble table, per the FIT SDK spec)."""
    crc = 0
    for byte in data:
        tmp = _CRC[crc & 0xF]
        crc = ((crc >> 4) & 0x0FFF) ^ tmp ^ _CRC[byte & 0xF]
        tmp = _CRC[crc & 0xF]
        crc = ((crc >> 4) & 0x0FFF) ^ tmp ^ _CRC[(byte >> 4) & 0xF]
    return crc


async def main():
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, ADDR)
    path_b = FILEPATH.encode("ascii")

    buf, total, mismatches, short_chunks, notif_count = bytearray(), None, [], [], 0
    t0 = time.time()

    async with NotifyStream(bus, char_path) as stream:
        print(f"mtu={stream.mtu}  chunk_len={CHUNK}")
        offset = 0
        while True:
            cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", offset)
                   + struct.pack("<I", CHUNK) + path_b)
            await write_command(bus, char_path, cmd)

            got_this_request = 0
            while True:
                try:
                    b = await stream.get(timeout=8.0)
                except asyncio.TimeoutError:
                    print(f"  TIMEOUT at offset={offset} after {got_this_request} bytes")
                    return report(buf, total, mismatches, short_chunks, notif_count, t0, hung=True)
                notif_count += 1
                if not b or b[0] != 0x11 or len(b) < 16:
                    print(f"  unexpected frame at offset={offset}: {b.hex()}")
                    return report(buf, total, mismatches, short_chunks, notif_count, t0, hung=True)
                off, total, rcl = struct.unpack("<III", b[4:16])
                payload = b[16:]
                if rcl != len(payload):
                    mismatches.append((off, rcl, len(payload)))
                if off != len(buf):
                    print(f"  OUT OF ORDER: declared offset={off}, expected {len(buf)}")
                buf += payload
                got_this_request += len(payload)
                if len(buf) >= total or got_this_request >= CHUNK:
                    break

            expected = min(CHUNK, total - offset)
            if got_this_request != expected:
                short_chunks.append((offset, expected, got_this_request))
            if len(buf) >= total:
                break
            offset += got_this_request

    report(buf, total, mismatches, short_chunks, notif_count, t0)


def report(buf, total, mismatches, short_chunks, notif_count, t0, hung=False):
    dt = time.time() - t0
    data = bytes(buf)
    print()
    print(f"declared total : {total}")
    print(f"received bytes : {len(data)}")
    print(f"notifications  : {notif_count}   elapsed {dt:.1f}s  ({len(data)/dt/1024:.1f} kB/s)")
    print(f"sha256         : {hashlib.sha256(data).hexdigest()}")
    print()
    print(f"[1] header-accounting mismatches (real_chunklen != payload): {len(mismatches)}  "
          f"{'FAIL' if mismatches else 'PASS'}")
    for m in mismatches[:5]:
        print(f"      offset={m[0]} real_chunklen={m[1]} payload={m[2]}")
    print(f"[2] requests delivering fewer bytes than requested: {len(short_chunks)}  "
          f"{'FAIL' if short_chunks else 'PASS'}")
    for s in short_chunks[:5]:
        print(f"      offset={s[0]} expected={s[1]} got={s[2]}")
    if hung:
        print("[3] FIT CRC-16: SKIPPED (transfer hung)")
        return
    if len(data) == total and total > 2:
        stored = struct.unpack("<H", data[-2:])[0]
        computed = fit_crc(data[:-2])
        print(f"[3] FIT CRC-16: stored=0x{stored:04X} computed=0x{computed:04X}  "
              f"{'PASS' if stored == computed else 'FAIL'}")
    else:
        print(f"[3] FIT CRC-16: SKIPPED (got {len(data)} of {total} bytes)")


asyncio.run(main())
