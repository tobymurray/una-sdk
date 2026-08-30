#!/usr/bin/env python3
"""
Standalone, human-in-the-loop-but-not-Claude-in-the-loop pairing helper for the UNA Watch.

Run this directly in your own terminal (not through an agent) -- it needs a human to read
the passkey off the watch's screen and type it in, and that has to happen fast: the watch's
own authentication-request timeout is short enough that relaying the code through a chat
assistant (read code -> tell assistant -> assistant runs a tool call -> assistant writes it
somewhere) reliably loses the race. Typing directly into this script's own prompt is the
fastest a human can plausibly do it.

What it does automatically (no typing needed for this part):
  - Registers a KeyboardOnly BlueZ pairing agent as the system default.
  - Watches BlueZ's whole D-Bus object tree continuously (both new-device and
    property-update events, since the watch's Name sometimes arrives after the object is
    first created) for a device named "UNA Watch 403795".
  - The instant a live, not-yet-attempted device object is seen, immediately calls Pair()
    on it -- no polling loop, no shell/text-log scraping. The watch's advertised address
    rotates fast while in pairing mode, so most single attempts will fail at the link layer
    (harmless "Page Timeout") before ever reaching authentication; the script just keeps
    watching and retries automatically on every new address until one actually connects.
  - Retries indefinitely until Ctrl+C.

What needs you:
  - The moment you see "ENTER PASSKEY NOW", read the 6-digit code off the watch screen and
    type it into THIS terminal, then press Enter. That's the only human step left.

On success, writes the resolved identity address to paired_address.txt next to this script,
so other tools in this folder (una_ble_client.py, auto_connect_pull.py) can pick it up.
"""
import asyncio
import os
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.service import ServiceInterface, method
from dbus_fast.message import Message
from dbus_fast.signature import Variant

TARGET_NAME = "UNA Watch 403795"
HERE = os.path.dirname(os.path.abspath(__file__))
ADDRESS_FILE = os.path.join(HERE, "paired_address.txt")

attempted_paths = set()
pairing_in_progress = asyncio.Lock()


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(destination="org.bluez", path=path, interface=iface,
                  member=member, signature=signature, body=body or [])
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


class Agent(ServiceInterface):
    def __init__(self, loop):
        super().__init__("org.bluez.Agent1")
        self.loop = loop

    @method()
    def Release(self):
        pass

    @method()
    def RequestPasskey(self, device: "o") -> "u":  # noqa: F821
        print("\n" + "=" * 60)
        print("ENTER PASSKEY NOW -- read the 6-digit code off the watch screen")
        print("=" * 60)
        while True:
            raw = input(">>> passkey: ").strip()
            if raw.isdigit():
                return int(raw)
            print("not a number, try again")

    @method()
    def RequestConfirmation(self, device: "o", passkey: "u"):  # noqa: F821
        print(f"auto-confirming displayed passkey {passkey:06d}")

    @method()
    def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):  # noqa: F821
        print(f"watch is displaying passkey {passkey:06d} (informational)")

    @method()
    def AuthorizeService(self, device: "o", uuid: "s"):  # noqa: F821
        pass

    @method()
    def Cancel(self):
        print("pairing attempt cancelled by peer, still watching for the next window...")


async def try_pair(bus, path, address):
    if path in attempted_paths:
        return
    attempted_paths.add(path)
    if pairing_in_progress.locked():
        return
    async with pairing_in_progress:
        print(f"[{address}] seen -- attempting to pair...")
        try:
            await call(bus, path, "org.bluez.Device1", "Pair")
            print(f"[{address}] PAIRED")
            await call(bus, path, "org.freedesktop.DBus.Properties", "Set",
                       "ssv", ["org.bluez.Device1", "Trusted", Variant("b", True)])
            print(f"[{address}] Trusted set")
            with open(ADDRESS_FILE, "w") as f:
                f.write(address + "\n")
            print(f"\nDONE. Resolved address written to {ADDRESS_FILE}")
            os._exit(0)
        except Exception as exc:
            print(f"[{address}] pair attempt failed ({exc}) -- still watching...")


async def main():
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    loop = asyncio.get_event_loop()
    agent = Agent(loop)
    bus.export("/una_investigation/agent", agent)
    await call(bus, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
               "os", ["/una_investigation/agent", "KeyboardOnly"])
    await call(bus, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
               "o", ["/una_investigation/agent"])
    print("Agent registered as default.")

    root_intro = await bus.introspect("org.bluez", "/")
    root_obj = bus.get_proxy_object("org.bluez", "/", root_intro)
    om = root_obj.get_interface("org.freedesktop.DBus.ObjectManager")

    def maybe_pair(path, dev_props):
        name = dev_props.get("Name")
        addr = dev_props.get("Address")
        if name and addr and name.value == TARGET_NAME:
            asyncio.ensure_future(try_pair(bus, path, addr.value))

    def on_interfaces_added(path, interfaces):
        dev = interfaces.get("org.bluez.Device1")
        if dev:
            maybe_pair(path, dev)

    om.on_interfaces_added(on_interfaces_added)

    # Name sometimes arrives via a later PropertiesChanged rather than at creation time --
    # subscribe per-device as we discover them so we don't miss that case.
    subscribed = set()

    async def subscribe_props(path):
        if path in subscribed:
            return
        subscribed.add(path)
        try:
            intro = await bus.introspect("org.bluez", path)
            obj = bus.get_proxy_object("org.bluez", path, intro)
            props_iface = obj.get_interface("org.freedesktop.DBus.Properties")
        except Exception:
            return

        def make_handler(bound_path):
            # dbus_fast's on_signal_fn requires the callback to take exactly the
            # signal's own positional parameters (3, for PropertiesChanged) -- no
            # extra defaulted params allowed -- so bind `path` via closure instead.
            def on_props_changed(interface, changed, invalidated):
                if interface == "org.bluez.Device1" and "Name" in changed:
                    asyncio.ensure_future(_recheck(bound_path))
            return on_props_changed

        props_iface.on_properties_changed(make_handler(path))

    async def _recheck(path):
        try:
            intro = await bus.introspect("org.bluez", path)
            obj = bus.get_proxy_object("org.bluez", path, intro)
            props_iface = obj.get_interface("org.freedesktop.DBus.Properties")
            all_props = await props_iface.call_get_all("org.bluez.Device1")
        except Exception:
            return
        maybe_pair(path, all_props)

    async def on_interfaces_added_sub(path, interfaces):
        if "org.bluez.Device1" in interfaces:
            await subscribe_props(path)

    om.on_interfaces_added(lambda p, i: asyncio.ensure_future(on_interfaces_added_sub(p, i)))

    for path, ifaces in (await om.call_get_managed_objects()).items():
        if "org.bluez.Device1" in ifaces:
            await subscribe_props(path)
            maybe_pair(path, ifaces["org.bluez.Device1"])

    try:
        await call(bus, "/org/bluez/hci0", "org.bluez.Adapter1", "StartDiscovery")
    except RuntimeError as exc:
        if "InProgress" not in str(exc):
            raise

    print(f"Watching for '{TARGET_NAME}' -- put the watch in pairing mode now.")
    print("(Ctrl+C to stop)")
    while True:
        await asyncio.sleep(3600)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nstopped")
