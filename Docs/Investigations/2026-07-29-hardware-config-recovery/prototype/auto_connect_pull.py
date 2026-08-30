#!/usr/bin/env python3
"""
Background auto-connect watcher for the UNA Watch.

The watch only advertises in a short, low-duty-cycle connectable window once bonded
(see BLE-COMPANION-protocol-spec.md sect 6a) -- one-shot `bluetoothctl connect` calls
reliably lose the race against that window. This mimics what a phone's BLE stack does:
keep discovery running continuously, react to the *first* sign of life from the device
(an advertisement report / RSSI update) and fire Connect() immediately, rather than
polling on a fixed timer. It also makes sure BlueZ has been told to Trust the device,
since BlueZ's own kernel-level auto-reconnect for LE peripherals is gated on that flag.

Once connected, it runs the requested FTS operation (list or read) using the same
protocol implementation as una_ble_client.py, then exits.

Usage:
    python3 auto_connect_pull.py <device-address> read <path> [output-file]
    python3 auto_connect_pull.py <device-address> list <path>
"""
import asyncio
import os
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

sys.path.insert(0, os.path.dirname(__file__))
from una_ble_client import find_fts_characteristic, list_dir, read_file  # noqa: E402


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(
        destination="org.bluez", path=path, interface=iface,
        member=member, signature=signature, body=body or [],
    )
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


async def find_device_path(bus, device_address: str):
    """Returns the object path for device_address. Note this is NOT simply
    /org/bluez/hci0/dev_<address> -- BlueZ names the object after whatever raw
    (possibly rotating) address it first saw the device advertise under, and the
    `Address` property is updated separately once IRK-based identity resolution
    runs. Must always be looked up via ObjectManager, never reconstructed."""
    om_introspection = await bus.introspect("org.bluez", "/")
    om_obj = bus.get_proxy_object("org.bluez", "/", om_introspection)
    om = om_obj.get_interface("org.freedesktop.DBus.ObjectManager")
    objects = await om.call_get_managed_objects()
    for path, ifaces in objects.items():
        dev_iface = ifaces.get("org.bluez.Device1")
        if dev_iface and dev_iface["Address"].value.upper() == device_address.upper():
            connected = dev_iface.get("Connected")
            return path, bool(connected.value) if connected else False
    return None, False


async def wait_for_connection(bus, device_address: str, adapter_path="/org/bluez/hci0"):
    """Keep discovery alive and fire Connect() the instant the device is seen,
    instead of blindly retrying Connect() on a timer."""
    dev_path, connected = await find_device_path(bus, device_address)
    if dev_path is None:
        raise RuntimeError(f"{device_address} is not known to BlueZ -- pair it first")

    print("ensuring device is trusted (required for BlueZ background auto-connect)...")
    await call(bus, dev_path, "org.freedesktop.DBus.Properties", "Set",
               "ssv", ["org.bluez.Device1", "Trusted", Variant("b", True)])

    if connected:
        print("already connected")
        return dev_path

    dev_introspection = await bus.introspect("org.bluez", dev_path)
    dev_obj = bus.get_proxy_object("org.bluez", dev_path, dev_introspection)
    props_iface = dev_obj.get_interface("org.freedesktop.DBus.Properties")

    root_introspection = await bus.introspect("org.bluez", "/")
    root_obj = bus.get_proxy_object("org.bluez", "/", root_introspection)
    om = root_obj.get_interface("org.freedesktop.DBus.ObjectManager")

    seen_event = asyncio.Event()
    connect_lock = asyncio.Lock()
    connected_flag = {"v": False}

    async def try_connect(reason: str):
        if connected_flag["v"]:
            return
        async with connect_lock:
            if connected_flag["v"]:
                return
            try:
                print(f"  -> connect attempt (trigger: {reason})")
                await asyncio.wait_for(
                    call(bus, dev_path, "org.bluez.Device1", "Connect"), timeout=10.0
                )
                connected_flag["v"] = True
                print("  -> CONNECTED")
            except Exception as exc:  # noqa: BLE001 -- log and keep watching
                print(f"  -> connect failed: {exc}")

    def on_props_changed(interface, changed, invalidated):
        if interface != "org.bluez.Device1":
            return
        if any(k in changed for k in ("RSSI", "ManufacturerData", "Connected")):
            seen_event.set()

    def on_interfaces_added(path, interfaces):
        if path == dev_path and "org.bluez.Device1" in interfaces:
            seen_event.set()

    props_iface.on_properties_changed(on_props_changed)
    om.on_interfaces_added(on_interfaces_added)

    try:
        await call(bus, adapter_path, "org.bluez.Adapter1", "StartDiscovery")
    except RuntimeError as exc:
        if "InProgress" not in str(exc):
            raise

    print(f"watching for {device_address} to advertise (Ctrl+C to stop)...")

    async def watcher_loop():
        while not connected_flag["v"]:
            try:
                await asyncio.wait_for(seen_event.wait(), timeout=5.0)
                seen_event.clear()
                await try_connect("advertisement/property-change seen")
            except asyncio.TimeoutError:
                # Also poll periodically in case a signal was missed --
                # cheap insurance, not the primary trigger.
                await try_connect("periodic poll")

    await watcher_loop()
    return dev_path


async def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    address, op, path = sys.argv[1], sys.argv[2], sys.argv[3]

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()

    await wait_for_connection(bus, address)

    # Give BlueZ a moment to resolve GATT services after connect.
    await asyncio.sleep(2.0)
    char_path = await find_fts_characteristic(bus, address)

    if op == "list":
        entries = await list_dir(bus, char_path, path)
        for index, total, attr, name in entries:
            print(f"  [{index + 1}/{total}] attr=0x{attr:08x}  {name}")
    elif op == "read":
        print(f"reading {path} ...")
        data = await read_file(bus, char_path, path)
        if data is None:
            print("read failed / file not found")
            sys.exit(1)
        outpath = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(path)
        with open(outpath, "wb") as f:
            f.write(data)
        print(f"wrote {len(data)} bytes to {outpath}")
    else:
        print(f"unknown operation: {op!r} (expected 'list' or 'read')")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
