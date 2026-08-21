#!/usr/bin/env python3
"""List one directory over FTS. Read-only."""
import asyncio, importlib.util, sys
spec = importlib.util.spec_from_file_location("v5", "una_v5_probe.py")
v5 = importlib.util.module_from_spec(spec); spec.loader.exec_module(v5)

async def main():
    address, paths = sys.argv[1], sys.argv[2:]
    recorder = v5.Recorder(None)
    bus = await v5.MessageBus(bus_type=v5.BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await v5.find_device(bus, address)
    await v5.ensure_connected(bus, device_path)
    for _ in range(10):
        objects = await v5.managed_objects(bus)
        if v5.find_char(objects, device_path, v5.FTS_TRANSFER_CHAR): break
        await asyncio.sleep(1.0)
    char = v5.find_char(objects, device_path, v5.FTS_TRANSFER_CHAR)
    async with v5.NotifyStream(bus, char) as stream:
        for p in paths:
            entries = await v5.listdir(bus, char, stream, recorder, p, 8.0)
            print(f"\n== {p}  ({len(entries)} entries)")
            for e in entries:
                print(f"   {'d' if e['is_dir'] else 'f'} {e['size']:>8}  {e['name']}")
asyncio.run(main())
