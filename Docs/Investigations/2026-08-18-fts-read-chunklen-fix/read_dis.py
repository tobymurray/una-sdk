#!/usr/bin/env python3
"""Read Device Information Service strings (firmware/hardware/software revision) over BlueZ."""
import asyncio, sys
from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

ADDR = sys.argv[1]

DIS = {
    "00002a24": "Model Number",
    "00002a25": "Serial Number",
    "00002a26": "Firmware Revision",
    "00002a27": "Hardware Revision",
    "00002a28": "Software Revision",
    "00002a29": "Manufacturer",
}

async def main():
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    intro = await bus.introspect("org.bluez", "/")
    om = bus.get_proxy_object("org.bluez", "/", intro).get_interface(
        "org.freedesktop.DBus.ObjectManager")
    objects = await om.call_get_managed_objects()

    root = None
    for path, ifaces in objects.items():
        d = ifaces.get("org.bluez.Device1")
        if d and d["Address"].value.upper() == ADDR.upper():
            root = path
            break
    if not root:
        print("device not found"); return

    for path, ifaces in sorted(objects.items()):
        if not path.startswith(root):
            continue
        c = ifaces.get("org.bluez.GattCharacteristic1")
        if not c:
            continue
        uuid = c["UUID"].value
        label = DIS.get(uuid[:8])
        if not label:
            continue
        msg = Message(destination="org.bluez", path=path,
                      interface="org.bluez.GattCharacteristic1",
                      member="ReadValue", signature="a{sv}", body=[{}])
        reply = await bus.call(msg)
        if reply.message_type.name == "ERROR":
            print(f"{label:20s}: <read failed: {reply.body}>")
        else:
            print(f"{label:20s}: {bytes(reply.body[0]).decode('utf-8','replace').strip(chr(0))}")

asyncio.run(main())
