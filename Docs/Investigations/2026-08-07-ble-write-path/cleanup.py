#!/usr/bin/env python3
"""Delete every ble_bench_* scratch file this investigation created under /Apps/HelloWorld/,
one bus connection for all of them. Reports per-file success/failure -- files with a write
session left open (an earlier attempt that errored out mid-transfer) are expected to fail to
delete; that's itself a finding, not a bug in this script."""
import asyncio
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_command, NotifyStream  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402

FILES = [
    "ble_bench_probe.bin", "ble_bench_test2.bin", "ble_bench_probe_100.bin",
    "ble_bench_probe_126.bin", "ble_bench_probe_200.bin", "ble_bench_probe_201.bin",
    "ble_bench_smoke1.bin", "ble_bench_check_fresh.bin", "ble_bench_step1.bin",
    "ble_bench_multi1.bin", "ble_bench_check1000.bin", "ble_bench_multi2.bin",
    "ble_bench_multi3.bin", "ble_bench_multi4.bin", "ble_bench_multi5.bin",
    "ble_bench_multi6.bin", "ble_bench_m5_1MiB.bin", "ble_bench_m5_3MiB.bin",
    "ble_bench_m5_8MiB.bin", "ble_bench_m5_29MiB.bin", "ble_bench_m6_resume.bin",
    "ble_bench_m6_wrongoffset.bin", "ble_bench_m6_outoforder.bin",
]


async def delete_one(bus, char_path, path):
    path_b = path.encode("ascii")
    async with NotifyStream(bus, char_path) as stream:
        cmd = b"\x30\x00" + struct.pack("<H", len(path_b)) + path_b
        await write_command(bus, char_path, cmd)
        try:
            b = await stream.get(timeout=8.0)
            if len(b) >= 2 and b[0] == 0x31 and b[1] == 0x01:
                return True, b.hex()
            return False, b.hex()
        except asyncio.TimeoutError:
            return False, "TIMEOUT"


async def main():
    address = sys.argv[1]
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    ok, failed = [], []
    for name in FILES:
        path = f"/Apps/HelloWorld/{name}"
        success, raw = await delete_one(bus, char_path, path)
        print(f"{'OK  ' if success else 'FAIL'} {name}  ({raw})")
        (ok if success else failed).append(name)
        await asyncio.sleep(0.2)

    print(f"\n{len(ok)}/{len(FILES)} deleted. Failed: {failed}")


if __name__ == "__main__":
    asyncio.run(main())
