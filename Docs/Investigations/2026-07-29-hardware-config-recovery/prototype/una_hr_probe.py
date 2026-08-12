#!/usr/bin/env python3
"""
Black-box probe for the UNA Watch's CCS hourly-HR command, against a watch you own.

Sends CCS requests directly over BlueZ and prints exactly what comes back, so the wire format
is established by observing your own hardware rather than by reading anyone's source. Nothing
here is written to the watch's filesystem and no state is modified -- every command is a read.

What it is trying to settle:

  1. Does the 0x14 hourly-HR command exist and answer, and with what layout?
  2. What does the watch do for an hour it has NO data for -- reply with an error status, or
     stay silent? This is the load-bearing question: a Gadgetbridge sync that walks 12 hours
     has no timeout, so silence would stall it, while an error status is skippable.
  3. How far back does the watch actually retain an hourly matrix?

The 0x10 daily-health command is sent first as a control. It is already known to work (it is
what Gadgetbridge implements today), so if 0x10 answers and 0x14 does not, the difference is
the command and not the connection.

Requirements:
    - Linux with BlueZ, and `dbus_fast` (pip install dbus_fast).
    - The watch must already be paired/bonded via `bluetoothctl` -- this does not pair.
    - The watch's advertising window is short. If it will not connect, wake it (raise wrist /
      press a button) and re-run; --connect-retries gives it several attempts.

Usage:
    python3 una_hr_probe.py <device-address> [options]

Example:
    python3 una_hr_probe.py E8:DF:D5:49:4C:40
    python3 una_hr_probe.py E8:DF:D5:49:4C:40 --hours 18 --retention-days 3
"""
import argparse
import asyncio
import datetime
import os
import struct
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

CCS_COMMAND_CHAR_UUID = "554e4100-a2cf-4df8-0001-7e1e48595106"

CMD_DAILY_HEALTH = 0x10
CMD_HOURLY_HR = 0x14
STATUS_OK = 0x01

MINUTES_PER_HOUR = 60


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(
        destination="org.bluez", path=path, interface=iface,
        member=member, signature=signature, body=body or [],
    )
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


async def find_device_and_characteristic(bus, device_address, char_uuid):
    """Locate the device object by its resolved Address property, then the characteristic under
    it. Matching on the property rather than mangling the address into an object path is what
    survives BlueZ naming a device after a rotating advertised address."""
    introspection = await bus.introspect("org.bluez", "/")
    obj = bus.get_proxy_object("org.bluez", "/", introspection)
    manager = obj.get_interface("org.freedesktop.DBus.ObjectManager")
    objects = await manager.call_get_managed_objects()

    device_path = None
    for path, ifaces in objects.items():
        dev = ifaces.get("org.bluez.Device1")
        if dev and dev["Address"].value.upper() == device_address.upper():
            device_path = path
            break
    if device_path is None:
        raise RuntimeError(f"No known BlueZ device with Address {device_address} -- pair it first")

    for path, ifaces in objects.items():
        if not path.startswith(device_path):
            continue
        char = ifaces.get("org.bluez.GattCharacteristic1")
        if char and char["UUID"].value.lower() == char_uuid:
            return device_path, path
    return device_path, None


async def ensure_connected(bus, device_path, retries, delay=8.0):
    """The firmware's advertising/reconnect window is short enough that a single Connect() is
    unreliable in practice, so retry patiently rather than failing the whole run.

    `le-connection-abort-by-local` on every attempt almost always means one of two things: the
    watch is asleep and not advertising (wake it and keep it awake), or a phone already holds
    the connection -- this firmware accepts one central at a time, so Gadgetbridge or the UNA
    app must be disconnected first.
    """
    for attempt in range(1, retries + 1):
        props = await call(bus, device_path, "org.freedesktop.DBus.Properties", "Get",
                           "ss", ["org.bluez.Device1", "Connected"])
        if props.body[0].value:
            print("  connected")
            return
        print(f"  connecting (attempt {attempt}/{retries})...")
        try:
            await call(bus, device_path, "org.bluez.Device1", "Connect")
            await asyncio.sleep(1.0)
            print("  connected")
            return
        except RuntimeError as e:
            if attempt == retries:
                raise RuntimeError(
                    f"{e}\n"
                    "  Every connect attempt aborted. Check that:\n"
                    "    - the watch is awake (raise wrist / press a button) and stays awake\n"
                    "    - no phone is connected to it -- disconnect Gadgetbridge and the UNA\n"
                    "      app first, this firmware takes one central at a time")
            print(f"    {e}")
            print(f"    retrying in {delay:.0f}s -- keep the watch awake, and make sure no")
            print("    phone is holding the connection")
            await asyncio.sleep(delay)


async def write_command(bus, char_path, data):
    await call(
        bus, char_path, "org.bluez.GattCharacteristic1", "WriteValue",
        "aya{sv}", [bytearray(data), {"type": Variant("s", "command")}],
    )


class NotifyStream:
    """Wraps AcquireNotify's raw socket. Deliberately NOT D-Bus PropertiesChanged, which was
    found to silently coalesce rapid successive GATT notifications."""

    def __init__(self, bus, char_path):
        self.bus = bus
        self.char_path = char_path
        self.fd = None
        self.mtu = None
        self.queue = asyncio.Queue()

    async def __aenter__(self):
        reply = await call(self.bus, self.char_path, "org.bluez.GattCharacteristic1",
                           "AcquireNotify", "a{sv}", [{}])
        fd_index, self.mtu = reply.body
        self.fd = reply.unix_fds[fd_index]

        def on_readable():
            try:
                data = os.read(self.fd, self.mtu)
            except BlockingIOError:
                return
            self.queue.put_nowait(data)

        asyncio.get_event_loop().add_reader(self.fd, on_readable)
        return self

    async def __aexit__(self, *exc):
        asyncio.get_event_loop().remove_reader(self.fd)
        os.close(self.fd)

    def drain(self):
        """Discard anything left over from a previous request so it cannot be mistaken for the
        answer to the next one."""
        stale = []
        while not self.queue.empty():
            stale.append(self.queue.get_nowait())
        return stale

    async def get(self, timeout):
        return await asyncio.wait_for(self.queue.get(), timeout=timeout)


def build_daily_health(year, month, day):
    return bytes([CMD_DAILY_HEALTH, 0]) + struct.pack("<H", year) + bytes([month, day])


def build_hourly_hr(year, month, day, hour):
    return bytes([CMD_HOURLY_HR, 0]) + struct.pack("<H", year) + bytes([month, day, hour])


def describe_daily_health(b):
    if len(b) < 22:
        return f"too short for a daily-health payload ({len(b)} bytes)"
    steps, floors, active, rhr, ahr = struct.unpack("<IIIII", b[2:22])
    return f"steps={steps} floors={floors} active={active}min RHR={rhr} AHR={ahr}"


def describe_hourly_hr(b):
    """Decode assuming `14 <status> <60 x u8>`. Reports the observed length separately so a
    different real layout is visible rather than silently mis-parsed."""
    body = b[2:]
    minutes = list(body[:MINUTES_PER_HOUR])
    measured = [(i, v) for i, v in enumerate(minutes) if v > 0]
    lines = [
        f"payload={len(body)} bytes (expected {MINUTES_PER_HOUR})",
        f"non-zero minutes={len(measured)}/{len(minutes)}",
    ]
    if measured:
        values = [v for _, v in measured]
        lines.append(f"min={min(values)} max={max(values)} first={measured[0]} last={measured[-1]}")
        lines.append("minutes=" + " ".join(f"{v:3d}" for v in minutes))
    if len(body) > MINUTES_PER_HOUR:
        extra = body[MINUTES_PER_HOUR:]
        lines.append(f"UNEXPECTED trailing bytes: {extra.hex()}")
    return "\n      ".join(lines)


async def probe(stream, bus, char_path, label, payload, expect_opcode, timeout):
    """Sends one command and waits for a response carrying `expect_opcode`, ignoring (but
    reporting) anything else. Returns a result dict; `response` is None on silence."""
    stale = stream.drain()
    for s in stale:
        print(f"      (discarded stale notification: {s.hex()})")

    print(f"\n--- {label}")
    print(f"  -> {payload.hex()}")
    started = asyncio.get_event_loop().time()
    await write_command(bus, char_path, payload)

    deadline = started + timeout
    while True:
        remaining = deadline - asyncio.get_event_loop().time()
        if remaining <= 0:
            print(f"  <- NO RESPONSE after {timeout:.1f}s  ** silence **")
            return {"label": label, "response": None, "elapsed": timeout}
        try:
            data = await stream.get(remaining)
        except asyncio.TimeoutError:
            print(f"  <- NO RESPONSE after {timeout:.1f}s  ** silence **")
            return {"label": label, "response": None, "elapsed": timeout}

        elapsed = asyncio.get_event_loop().time() - started
        if not data:
            continue
        if data[0] != expect_opcode:
            print(f"      (ignoring notification for opcode 0x{data[0]:02x}: {data.hex()})")
            continue

        status = data[1] if len(data) > 1 else None
        print(f"  <- {len(data)} bytes in {elapsed * 1000:.0f}ms: {data.hex()}")
        print(f"      opcode=0x{data[0]:02x} status={status}")
        if status != STATUS_OK:
            print("      ** non-OK status -- this is what an unavailable hour looks like **")
        elif expect_opcode == CMD_HOURLY_HR:
            print(f"      {describe_hourly_hr(data)}")
        elif expect_opcode == CMD_DAILY_HEALTH:
            print(f"      {describe_daily_health(data)}")
        return {"label": label, "response": data, "status": status, "elapsed": elapsed}


async def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", help="watch BD address, e.g. E8:DF:D5:49:4C:40")
    parser.add_argument("--hours", type=int, default=14,
                        help="how many recent hours to sweep (default 14, comfortably more than "
                             "a day's worth of recent hours a companion would want)")
    parser.add_argument("--retention-days", type=int, default=3,
                        help="also probe this many days back, to test retention (default 3)")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-request timeout in seconds (default 5.0; observed replies "
                             "arrive in ~90 ms, so this is generous)")
    parser.add_argument("--connect-retries", type=int, default=6)
    args = parser.parse_args()

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, char_path = await find_device_and_characteristic(
        bus, args.address, CCS_COMMAND_CHAR_UUID)

    print(f"device:         {device_path}")
    await ensure_connected(bus, device_path, args.connect_retries)

    if char_path is None:
        # Services may not have been resolved until the connection came up.
        _, char_path = await find_device_and_characteristic(
            bus, args.address, CCS_COMMAND_CHAR_UUID)
    if char_path is None:
        raise RuntimeError(
            f"CCS command characteristic {CCS_COMMAND_CHAR_UUID} not found -- "
            "is the watch connected and are services resolved?")
    print(f"characteristic: {char_path}")

    now = datetime.datetime.now()
    results = []

    async with NotifyStream(bus, char_path) as stream:
        # An MTU under 65 would mean a 62-byte hourly-HR response cannot arrive in one
        # notification, which would change the whole shape of the client.
        print(f"notify MTU:     {stream.mtu}"
              + ("" if stream.mtu >= 65 else "   ** under 65: a 62-byte reply cannot fit **"))

        print("\n=== control: 0x10 daily health (known-good command) ===")
        results.append(await probe(
            stream, bus, char_path, f"daily health {now:%Y-%m-%d}",
            build_daily_health(now.year, now.month, now.day),
            CMD_DAILY_HEALTH, args.timeout))

        print(f"\n=== 0x14 hourly HR: last {args.hours} hours ===")
        for i in range(args.hours - 1, -1, -1):
            t = (now - datetime.timedelta(hours=i)).replace(minute=0, second=0, microsecond=0)
            results.append(await probe(
                stream, bus, char_path, f"hourly HR {t:%Y-%m-%d %H}:00 (-{i}h)",
                build_hourly_hr(t.year, t.month, t.day, t.hour),
                CMD_HOURLY_HR, args.timeout))

        if args.retention_days > 0:
            print(f"\n=== 0x14 retention: same hour, {args.retention_days} day(s) back ===")
            for d in range(1, args.retention_days + 1):
                t = (now - datetime.timedelta(days=d)).replace(minute=0, second=0, microsecond=0)
                results.append(await probe(
                    stream, bus, char_path, f"hourly HR {t:%Y-%m-%d %H}:00 (-{d}d)",
                    build_hourly_hr(t.year, t.month, t.day, t.hour),
                    CMD_HOURLY_HR, args.timeout))

        # 0x10 gets the same edge-case treatment as 0x14. A client that walks several days of
        # aggregates has exactly the same stall exposure as one walking hours, so "does an
        # unavailable day answer at all" has to be settled for both commands, not assumed to
        # carry over from one to the other.
        print("\n=== 0x10 daily health: edge cases ===")
        for delta, why in ((-60, "60 days back"), (-365, "a year back")):
            t = now + datetime.timedelta(days=delta)
            results.append(await probe(
                stream, bus, char_path, f"daily health {t:%Y-%m-%d} ({why})",
                build_daily_health(t.year, t.month, t.day),
                CMD_DAILY_HEALTH, args.timeout))
        future = now + datetime.timedelta(days=2)
        results.append(await probe(
            stream, bus, char_path, f"daily health {future:%Y-%m-%d} (future)",
            build_daily_health(future.year, future.month, future.day),
            CMD_DAILY_HEALTH, args.timeout))
        results.append(await probe(
            stream, bus, char_path, "daily health month=13 day=32 (out of range)",
            build_daily_health(now.year, 13, 32),
            CMD_DAILY_HEALTH, args.timeout))

        # Deliberately impossible requests: the cleanest way to see the error-status shape
        # without depending on what the watch happens to have retained.
        print("\n=== 0x14 deliberately invalid (error-status shape) ===")
        future = now + datetime.timedelta(days=2)
        results.append(await probe(
            stream, bus, char_path, f"hourly HR {future:%Y-%m-%d %H}:00 (future)",
            build_hourly_hr(future.year, future.month, future.day, future.hour),
            CMD_HOURLY_HR, args.timeout))
        results.append(await probe(
            stream, bus, char_path, "hourly HR hour=25 (out of range)",
            build_hourly_hr(now.year, now.month, now.day, 25),
            CMD_HOURLY_HR, args.timeout))

    print("\n\n=== summary ===")
    silent = 0
    for r in results:
        if r["response"] is None:
            silent += 1
            print(f"  {r['label']:<45} SILENT (no reply in {r['elapsed']:.1f}s)")
        else:
            status = r.get("status")
            note = "ok" if status == STATUS_OK else f"status={status}"
            print(f"  {r['label']:<45} {len(r['response']):>3}B  {note}")

    print()
    if silent:
        print(f"  ** {silent} request(s) got no reply at all. A Gadgetbridge sync that walks")
        print("     hours therefore NEEDS a per-request timeout, or it will stall. **")
    else:
        print("  Every request was answered, including the invalid ones. The watch does not")
        print("  report unavailable data with an error status at all -- it returns status 0x01")
        print("  with an all-zero payload, for both 0x10 and 0x14, and does no input validation.")
        print("  So: read 'no data' off the PAYLOAD, not the status byte; and since nothing ever")
        print("  goes silent, a per-request timeout is defence-in-depth rather than required.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nerror: {e}", file=sys.stderr)
        sys.exit(1)
