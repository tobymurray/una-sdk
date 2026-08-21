#!/usr/bin/env python3
"""Fetch and print one file over FTS. Read-only."""
import asyncio, importlib.util, sys

spec = importlib.util.spec_from_file_location("v5", "una_v5_probe.py")
v5 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v5)


async def main():
    address, paths = sys.argv[1], sys.argv[2:]
    recorder = v5.Recorder(None)
    bus = await v5.MessageBus(bus_type=v5.BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await v5.find_device(bus, address)
    await v5.ensure_connected(bus, device_path)
    for _ in range(10):
        objects = await v5.managed_objects(bus)
        if v5.find_char(objects, device_path, v5.FTS_TRANSFER_CHAR):
            break
        await asyncio.sleep(1.0)
    char = v5.find_char(objects, device_path, v5.FTS_TRANSFER_CHAR)
    async with v5.NotifyStream(bus, char) as stream:
        for path in paths:
            data, _ = await v5.read_file(bus, char, stream, recorder, path, 4096, 8.0, label="fetch")
            print(f"\n===== {path} =====")
            print(data.decode(errors="replace") if data else "(no data)")


asyncio.run(main())
