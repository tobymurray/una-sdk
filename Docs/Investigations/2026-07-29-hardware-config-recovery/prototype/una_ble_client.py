#!/usr/bin/env python3
"""
Standalone, phone-free BLE client for the UNA Watch's File Transfer Service (FTS).

Validated against a real watch: lists directories and pulls .fit activity files with a
correct FIT header and matching CRC-16. See ../BLE-COMPANION-protocol-spec.md for the full
protocol writeup (§2.2 framing, §4 auth, §6a prototype notes and known caveats).

Requirements:
    - Linux with BlueZ (system D-Bus), a Bluetooth adapter, and `dbus_fast` installed
      (pip install dbus_fast).
    - The watch must already be paired/bonded via `bluetoothctl` first (this script does not
      do pairing itself - BlueZ's SMP passkey agent flow is easiest driven interactively).
    - The watch's advertising window is short; if commands here time out, wake the watch
      (raise wrist / press a button) and retry.

Usage:
    python3 una_ble_client.py <device-address> list <path>
    python3 una_ble_client.py <device-address> read <path> [output-file]

Example:
    python3 una_ble_client.py E8:DF:D5:49:4C:40 list /Apps/
    python3 una_ble_client.py E8:DF:D5:49:4C:40 read \\
        /Apps/GpsLab/ActivityArchive/202607/activity_20260730T230037.fit out.fit
"""
import asyncio
import os
import struct
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

FTS_CHAR_UUID = "adaf0002-4669-6c65-5472-616e73666572"


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(
        destination="org.bluez", path=path, interface=iface,
        member=member, signature=signature, body=body or [],
    )
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


async def find_fts_characteristic(bus, device_address: str) -> str:
    """Walk BlueZ's whole object tree to find the FTS characteristic under the device whose
    resolved `Address` property matches device_address.

    Matching by the Address *property* rather than guessing the object path from the address
    string is what makes this portable across sessions -- BlueZ names a device's D-Bus object
    path after whatever raw (possibly rotating) address it first saw it advertise under, which
    is not necessarily the resolved identity address you paired with.
    """
    om_introspection = await bus.introspect("org.bluez", "/")
    om_obj = bus.get_proxy_object("org.bluez", "/", om_introspection)
    om = om_obj.get_interface("org.freedesktop.DBus.ObjectManager")
    objects = await om.call_get_managed_objects()

    device_root = None
    for path, ifaces in objects.items():
        dev_iface = ifaces.get("org.bluez.Device1")
        if dev_iface and dev_iface["Address"].value.upper() == device_address.upper():
            device_root = path
            break
    if device_root is None:
        raise RuntimeError(f"No known BlueZ device object with Address {device_address}")

    for path, ifaces in objects.items():
        if not path.startswith(device_root):
            continue
        char_iface = ifaces.get("org.bluez.GattCharacteristic1")
        if char_iface and char_iface["UUID"].value.lower() == FTS_CHAR_UUID:
            return path
    raise RuntimeError(
        f"FTS characteristic {FTS_CHAR_UUID} not found under {device_root} -- "
        "is the device connected and services resolved?"
    )


async def write_command(bus, char_path, data: bytes):
    await call(
        bus, char_path, "org.bluez.GattCharacteristic1", "WriteValue",
        "aya{sv}", [bytearray(data), {"type": Variant("s", "command")}],
    )


class NotifyStream:
    """Wraps AcquireNotify's raw socket -- deliberately NOT using D-Bus PropertiesChanged,
    which was found to silently coalesce/drop rapid successive GATT notifications."""

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
        loop = asyncio.get_event_loop()

        def on_readable():
            try:
                data = os.read(self.fd, self.mtu)
            except BlockingIOError:
                return
            self.queue.put_nowait(data)

        loop.add_reader(self.fd, on_readable)
        return self

    async def __aexit__(self, *exc):
        asyncio.get_event_loop().remove_reader(self.fd)
        os.close(self.fd)

    async def get(self, timeout: float):
        return await asyncio.wait_for(self.queue.get(), timeout=timeout)


async def list_dir(bus, char_path: str, dirpath: str):
    """FTS list-directory: 0x50 <flag=0x00> <path_len:u16LE> <path>, streamed 0x51 entries."""
    entries = []
    seen_idx = set()
    async with NotifyStream(bus, char_path) as stream:
        path_b = dirpath.encode("ascii")
        cmd = b"\x50\x00" + struct.pack("<H", len(path_b)) + path_b
        await write_command(bus, char_path, cmd)

        total = None
        while total is None or len(seen_idx) < total:
            try:
                b = await stream.get(timeout=8.0)
            except asyncio.TimeoutError:
                break
            if not b or b[0] != 0x51 or len(b) < 28:
                continue
            name_len, index, total, attr = struct.unpack("<HIII", b[2:16])
            name = b[28:28 + name_len].decode("ascii", errors="replace")
            if index not in seen_idx:
                seen_idx.add(index)
                entries.append((index, total, attr, name))
    return sorted(entries)


async def read_file(bus, char_path: str, filepath: str, chunk_len: int = 128):
    """FTS whole-file read: 0x10 request per chunk (offset advancing), 0x11 response per chunk.

    NOTE: a single 0x10 request does NOT stream the whole file on this firmware/setup --
    one request per chunk is required. See spec doc §6a for the full story; this differs
    from what an earlier phone-capture analysis assumed.

    offset/total/chunklen are genuine 32-bit fields (matching Adafruit's real BLE File Transfer
    Service spec byte-for-byte -- see spec doc §1.2/§2.2). An earlier version of this function
    parsed them as 16-bit and silently discarded the high half, which produced a false "64KB
    ceiling" finding on a real >64KB file -- retracted once a live phone-sync capture showed the
    high 16 bits were non-zero and combined with the low 16 bits into the correct true size.
    """
    path_b = filepath.encode("ascii")
    chunks = {}
    total_size = None

    async def request_chunk(bus_, offset):
        cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", offset)
               + struct.pack("<I", chunk_len) + path_b)
        await write_command(bus_, char_path, cmd)

    async with NotifyStream(bus, char_path) as stream:
        offset = 0
        while True:
            await request_chunk(bus, offset)
            try:
                b = await stream.get(timeout=8.0)
            except asyncio.TimeoutError:
                break
            if not b or b[0] != 0x11 or len(b) < 16:
                break
            got_offset, total, real_chunklen = struct.unpack("<III", b[4:16])
            payload = b[16:16 + real_chunklen]
            total_size = total
            chunks[got_offset] = payload[:max(0, total - got_offset)]
            if got_offset + real_chunklen >= total:
                break
            offset = got_offset + real_chunklen

    if total_size is None:
        return None
    buf = bytearray(total_size)
    for off, payload in chunks.items():
        buf[off:off + len(payload)] = payload
    return bytes(buf)


async def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    address, op, path = sys.argv[1], sys.argv[2], sys.argv[3]

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    if op == "list":
        entries = await list_dir(bus, char_path, path)
        for index, total, attr, name in entries:
            print(f"  [{index + 1}/{total}] attr=0x{attr:08x}  {name}")
    elif op == "read":
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
