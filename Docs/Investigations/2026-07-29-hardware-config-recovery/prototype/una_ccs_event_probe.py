#!/usr/bin/env python3
"""
Passive listener for the CCS event characteristic, the one channel the watch talks on unprompted.

CCS `-0002-` is notify-only and had never been observed carrying anything until 2026-08-21, when
it emitted `04 00 00` at subscribe time in two separate runs with the watch untouched. The
firmware names two senders -- `sendEventActivityEnded` and `sendEventFindPhoneAlert` -- and which
one `0x04` is, and what the two trailing bytes mean, is unknown.

This subscribes and does nothing else: it never writes to CCS, so nothing here can reach
`resetHandler` or `firmwareUpdateHandler`, which share that service. Every frame is timestamped
against the wearer's actions, so an opcode can be attributed to the thing that caused it.
"""
import argparse
import asyncio
import datetime
import importlib.util
import sys

spec = importlib.util.spec_from_file_location("cans", "una_cans_probe.py")
cans = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cans)

CCS_EVENT_CHAR = "554e4100-a2cf-4df8-0002-7e1e48595106"


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("address")
    parser.add_argument("--log", default="ccs_events.jsonl")
    parser.add_argument("--seconds", type=float, default=180.0)
    args = parser.parse_args()

    recorder = cans.Recorder(args.log)
    bus = await cans.MessageBus(bus_type=cans.BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await cans.find_device(bus, args.address)
    await cans.ensure_connected(bus, device_path)
    for _ in range(10):
        objects = await cans.managed_objects(bus)
        if cans.find_char(objects, device_path, CCS_EVENT_CHAR)[0]:
            break
        await asyncio.sleep(1.0)
    path, flags = cans.find_char(objects, device_path, CCS_EVENT_CHAR)
    print(f"CCS event characteristic {flags}")

    started = asyncio.get_event_loop().time()
    async with cans.NotifyStream(bus, path, "CCS-0002", recorder) as stream:
        print("subscribed. Frames are timestamped from here.\n")
        print("  DO ON THE WATCH, pausing between each so they can be told apart:")
        print("    1. trigger find-phone")
        print("    2. start a workout, wait a few seconds, end it")
        print("    3. anything else that seems like it might notify the phone\n")
        deadline = started + args.seconds
        count = 0
        while asyncio.get_event_loop().time() < deadline:
            remaining = deadline - asyncio.get_event_loop().time()
            try:
                frame = await asyncio.wait_for(stream.queue.get(), timeout=min(remaining, 10.0))
            except asyncio.TimeoutError:
                continue
            except RuntimeError as e:
                print(f"  {e}")
                break
            count += 1
            elapsed = asyncio.get_event_loop().time() - started
            opcode = frame[0] if frame else None
            print(f"  [{elapsed:6.1f}s] {frame.hex():<20} opcode=0x{opcode:02x} len={len(frame)}"
                  if frame else f"  [{elapsed:6.1f}s] empty")
            recorder.record("ccs_event", elapsed=round(elapsed, 1), hex=frame.hex(),
                            opcode=opcode, length=len(frame))
        print(f"\n  {count} frame(s) in {args.seconds:.0f}s")
    recorder.close()


asyncio.run(main())
