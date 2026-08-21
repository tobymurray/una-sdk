#!/usr/bin/env python3
"""Is the CANS write reaching the watch at all?

write-without-response is fire-and-forget: a silently dropped write and a watch that ignores a
delivered write look identical from here. If the characteristic also accepts a write WITH response
then success is an ATT-level acknowledgement, which separates "not delivered" from "delivered and
ignored" -- the two explanations that have been indistinguishable all session.

Also writes a deliberately malformed frame. The firmware logs `Unsupported CANS::ActionID %d`, so
if a bad action is treated differently from a good one anywhere observable, that is a parse
reaching the handler.
"""
import asyncio, importlib.util, struct, sys

spec = importlib.util.spec_from_file_location("cans", "una_cans_probe.py")
cans = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cans)


async def try_write(bus, path, data, write_type, label):
    try:
        await cans.call(bus, path, "org.bluez.GattCharacteristic1", "WriteValue",
                        "aya{sv}", [bytearray(data), {"type": cans.Variant("s", write_type)}])
        print(f"  {label:38s} type={write_type:8s} -> accepted")
        return True
    except Exception as e:
        print(f"  {label:38s} type={write_type:8s} -> {str(e)[:90]}")
        return False


async def main():
    address = sys.argv[1]
    bus = await cans.MessageBus(bus_type=cans.BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await cans.find_device(bus, address)
    await cans.ensure_connected(bus, device_path)
    for _ in range(10):
        objects = await cans.managed_objects(bus)
        if cans.find_char(objects, device_path, cans.CANS_NOTIFY_CHAR)[0]:
            break
        await asyncio.sleep(1.0)
    notify_path, notify_flags = cans.find_char(objects, device_path, cans.CANS_NOTIFY_CHAR)
    command_path, command_flags = cans.find_char(objects, device_path, cans.CANS_COMMAND_CHAR)
    print(f"CANS-0001 flags {notify_flags}\nCANS-0002 flags {command_flags}\n")

    good = cans.build_event(8957800, 0, 1)
    await try_write(bus, notify_path, good, "command", "well-formed event")
    await try_write(bus, notify_path, good, "request", "well-formed event")
    await try_write(bus, notify_path, bytes([0x01, 0x7F]) + struct.pack("<I", 8957801) + bytes([1]),
                    "command", "invalid ActionID 0x7F")
    await try_write(bus, notify_path, bytes([0x01, 0x00]) + struct.pack("<I", 8957802) + bytes([0x7F]),
                    "command", "invalid CategoryID 0x7F")
    await try_write(bus, notify_path, b"\x01\x00", "command", "truncated 2-byte frame")
    await try_write(bus, notify_path, bytes(20), "command", "20 zero bytes")
    print("\n  (an ATT error on the 'request' attempt only means the characteristic is")
    print("   write-without-response; it does not mean the command writes were dropped)")


asyncio.run(main())
