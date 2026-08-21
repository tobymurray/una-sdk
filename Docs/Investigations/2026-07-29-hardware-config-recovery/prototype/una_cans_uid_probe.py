#!/usr/bin/env python3
"""
Tests whether the CANS notification UID is opaque or carries a timestamp the watch uses to
discard stale notifications.

Two events sent with small UIDs (9000, 9100) produced no reply and nothing on the watch's screen.
The firmware carries `[UID: %u] Skip old notification at %u, now %u`, and the vendor app builds a
UID as `(hash(appId + '-' + key) & 0xFF) << 24 | (a0 & 0xFFFFFF)` -- the low 24 bits are a
caller-supplied number, and `formatTimestamp` is defined immediately after it. If that number is a
clock reading, a small UID is a 1970 timestamp and the watch is right to drop it.

Sends one Add per candidate UID and listens. A candidate that draws a RequestAttributes is the
answer; the small-UID control is included so a run where nothing at all works is distinguishable
from one where the timestamp theory is simply wrong.

Read-only apart from CANS notification events. Usage:
    python3 una_cans_uid_probe.py <address>
"""
import argparse
import asyncio
import datetime
import importlib.util
import json
import struct
import sys
import time

spec = importlib.util.spec_from_file_location("cans", "una_cans_probe.py")
cans = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cans)


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("address")
    parser.add_argument("--log", default="cans_uid.jsonl")
    parser.add_argument("--settle", type=float, default=7.0)
    args = parser.parse_args()

    now_s = int(time.time())
    now_ms = int(time.time() * 1000)
    candidates = [
        ("control: small uid, known silent", 9200),
        ("unix seconds, truncated to 24 bits", now_s & 0xFFFFFF),
        ("unix seconds truncated, with a hash-like top byte", (now_s & 0xFFFFFF) | (0x42 << 24)),
        ("unix milliseconds, truncated to 24 bits", now_ms & 0xFFFFFF),
        ("all 24 low bits set (far future if it is a clock)", 0x00FFFFFF),
        ("every bit set", 0xFFFFFFFF),
        ("full 32-bit unix seconds, untruncated", now_s & 0xFFFFFFFF),
    ]

    recorder = cans.Recorder(args.log)
    recorder.record("run", address=args.address, now_s=now_s,
                    candidates=[{"label": l, "uid": u} for l, u in candidates])

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

    print(f"clock: local unix seconds {now_s}; low 24 bits {now_s & 0xFFFFFF}")
    hits = []
    async with cans.NotifyStream(bus, command_path, "CANS-0002", recorder) as stream:
        for label, uid in candidates:
            stream.drain()
            print(f"\n  uid={uid} (0x{uid:08x})  {label}")
            await cans.send_event(bus, chars, recorder, uid, cans.ACTION_ADD,
                                  cans.CATEGORY_MESSAGE, note=label)
            frames = await stream.collect(args.settle)
            decoded = cans.summarise(frames)
            print(f"    {len(frames)} frame(s): {decoded if decoded else '-'}")
            recorder.record("finding", question="uid-theory", label=label, uid=uid,
                            frames=len(frames), decoded=decoded)
            if frames:
                hits.append((label, uid, decoded))

    print("\n=== result ===")
    if hits:
        for label, uid, decoded in hits:
            print(f"  RESPONDED  uid={uid} (0x{uid:08x})  {label}")
            print(f"             {decoded}")
    else:
        print("  no candidate drew a reply -- the UID is not what is blocking this")
    recorder.record("summary", responded=[{"label": l, "uid": u} for l, u, _ in hits])
    recorder.close()
    print(f"\ntranscript: {args.log}")


if __name__ == "__main__":
    asyncio.run(main())
