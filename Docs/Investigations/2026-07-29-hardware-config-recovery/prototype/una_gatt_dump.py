#!/usr/bin/env python3
"""
Dump the UNA Watch's full GATT table -- every service, characteristic and property flag.

Costs nothing and risks nothing: BlueZ has already resolved this during connection, so this
reads its cache and puts no traffic on the air at all. Run it first in any session; the
characteristic *flags* in particular answer "which direction does this channel go", which is
otherwise guesswork.

The watch must be connected with services resolved (`bluetoothctl connect <address>`).

Usage:
    python3 una_gatt_dump.py [device-address]

With no address, dumps every connected device BlueZ knows about.
"""
import asyncio
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus

# Prefix -> human label. Matched by prefix so the 128-bit vendor UUIDs group cleanly.
KNOWN = {
    "00001800": "GAP",
    "00001801": "GATT",
    "0000180a": "DIS  (Device Information)",
    "0000180f": "BAS  (Battery)",
    "00001805": "CTS  (Current Time)",
    "0000febb": "FTS  (Adafruit 0xFEBB service UUID, UNA's own characteristic protocol)",
    "adaf0001": "FTS version/identity characteristic",
    "adaf0002": "FTS command+response characteristic",
    "554e4100-a2cf-4df8-0000": "CCS  (Custom Command Service)",
    "554e4100-a2cf-4df8-0001": "CCS command  (0x10 daily health, 0x14 hourly HR)",
    "554e4100-a2cf-4df8-0002": "CCS event    (watch->phone pushes)",
    "554e4100-28e7-4811-0000": "CANS (Custom Android Notification Service)",
    "554e4100-28e7-4811-0001": "CANS notification push",
    "554e4100-28e7-4811-0002": "CANS secondary/reply channel",
    "6e400001": "Nordic UART Service",
    "00002a19": "Battery Level",
    "00002a2b": "Current Time",
    "00002a0f": "Local Time Information",
    "00002a29": "Manufacturer Name",
    "00002a24": "Model Number",
    "00002a25": "Serial Number",
    "00002a26": "Firmware Revision",
    "00002a27": "Hardware Revision",
    "00002a00": "Device Name",
    "00002a01": "Appearance",
    "00002a04": "Preferred Connection Parameters",
    "00002aa6": "Central Address Resolution",
    "00002a05": "Service Changed",
}


def label(uuid):
    for prefix, name in KNOWN.items():
        if uuid.startswith(prefix):
            return name
    return ""


async def main():
    wanted = sys.argv[1].upper() if len(sys.argv) > 1 else None

    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    introspection = await bus.introspect("org.bluez", "/")
    obj = bus.get_proxy_object("org.bluez", "/", introspection)
    manager = obj.get_interface("org.freedesktop.DBus.ObjectManager")
    objects = await manager.call_get_managed_objects()

    devices = {}
    for path, ifaces in objects.items():
        dev = ifaces.get("org.bluez.Device1")
        if not dev:
            continue
        if wanted and dev["Address"].value.upper() != wanted:
            continue
        if not wanted and not dev.get("Connected", None):
            continue
        devices[path] = dev

    if not devices:
        print("no matching connected device -- connect it first with bluetoothctl")
        return

    for device_path, dev in devices.items():
        name = dev["Name"].value if "Name" in dev else "?"
        print(f"{name}  [{dev['Address'].value}]  connected={dev['Connected'].value} "
              f"servicesResolved={dev.get('ServicesResolved').value if 'ServicesResolved' in dev else '?'}")
        print(f"  object path: {device_path}")

        services = {p: i["org.bluez.GattService1"]
                    for p, i in objects.items()
                    if p.startswith(device_path + "/") and "org.bluez.GattService1" in i}

        for service_path in sorted(services):
            uuid = services[service_path]["UUID"].value
            print(f"\n  SERVICE {uuid}  {label(uuid)}")
            for char_path in sorted(objects):
                if not char_path.startswith(service_path + "/"):
                    continue
                char = objects[char_path].get("org.bluez.GattCharacteristic1")
                if not char:
                    continue
                cuuid = char["UUID"].value
                flags = ",".join(char["Flags"].value)
                print(f"    {cuuid}")
                print(f"        flags: [{flags}]")
                if label(cuuid):
                    print(f"        {label(cuuid)}")

        print(f"\n  totals: {len(services)} services, "
              f"{sum(1 for p, i in objects.items() if p.startswith(device_path + '/') and 'org.bluez.GattCharacteristic1' in i)}"
              " characteristics")


if __name__ == "__main__":
    asyncio.run(main())
