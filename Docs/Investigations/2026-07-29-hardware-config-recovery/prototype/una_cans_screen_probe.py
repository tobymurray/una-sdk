#!/usr/bin/env python3
"""
Tests whether the watch drops CANS events unless its notification app is on screen.

`Notification dropped: no callback attached` is in the firmware, alongside TouchGFX screens named
PhoneNotificationListView / DetailsView / ConfirmView. If a callback is only attached while that
app is loaded, a watch sitting on its watchface would discard every event -- which is exactly what
has been observed, and would not show up as an error anywhere a companion can see.

Waits for the wearer to open the notification screen, then sends events in each category. Read-only
apart from the notification events themselves.
"""
import argparse
import asyncio
import importlib.util
import time

spec = importlib.util.spec_from_file_location("cans", "una_cans_probe.py")
cans = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cans)


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("address")
    parser.add_argument("--log", default="cans_screen.jsonl")
    parser.add_argument("--lead", type=float, default=25.0)
    parser.add_argument("--settle", type=float, default=8.0)
    args = parser.parse_args()

    recorder = cans.Recorder(args.log)
    bus = await cans.MessageBus(bus_type=cans.BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await cans.find_device(bus, args.address)
    await cans.ensure_connected(bus, device_path)
    for _ in range(10):
        objects = await cans.managed_objects(bus)
        if cans.find_char(objects, device_path, cans.CANS_COMMAND_CHAR)[0]:
            break
        await asyncio.sleep(1.0)

    notify_path, _ = cans.find_char(objects, device_path, cans.CANS_NOTIFY_CHAR)
    command_path, _ = cans.find_char(objects, device_path, cans.CANS_COMMAND_CHAR)
    chars = {"cans_notify": notify_path, "cans_command": command_path}

    print(f"OPEN THE NOTIFICATIONS APP ON THE WATCH NOW -- sending in {args.lead:.0f}s")
    for remaining in range(int(args.lead), 0, -5):
        print(f"  {remaining}s")
        await asyncio.sleep(5)

    uid_base = int(time.time()) & 0xFFFFFF
    hits = []
    async with cans.NotifyStream(bus, command_path, "CANS-0002", recorder) as stream:
        for offset, (category, name) in enumerate(
                ((cans.CATEGORY_MESSAGE, "Message"), (cans.CATEGORY_CALL, "Call"),
                 (cans.CATEGORY_OTHER, "Other"))):
            uid = uid_base + offset
            stream.drain()
            print(f"\n  uid={uid} category={name}")
            await cans.send_event(bus, chars, recorder, uid, cans.ACTION_ADD, category,
                                  note=f"screen-open test, {name}")
            frames = await stream.collect(args.settle)
            decoded = cans.summarise(frames)
            print(f"    {len(frames)} frame(s): {decoded if decoded else '-'}")
            recorder.record("finding", question="screen-open", category=name, uid=uid,
                            frames=len(frames), decoded=decoded)
            if frames:
                hits.append((name, uid, decoded))

    print("\n=== result ===")
    print(f"  {len(hits)} category/ies drew a reply" if hits
          else "  still silent with the notification app open")
    for name, uid, decoded in hits:
        print(f"  RESPONDED {name} uid={uid}: {decoded}")
    recorder.close()


if __name__ == "__main__":
    asyncio.run(main())
