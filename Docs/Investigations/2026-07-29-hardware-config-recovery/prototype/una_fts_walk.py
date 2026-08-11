#!/usr/bin/env python3
"""
Read-only walk of the UNA Watch's filesystem over FTS, plus two small experiments.

Everything here is a read. It never sends the FTS write, delete, move or mkdir opcodes, and it
never sends the unexplained 0x30 command -- any of which could modify or destroy data on the
watch.

Three things it is trying to settle:

  1. What the activity archive layout actually is, so a companion can enumerate history instead
     of relying on `/Apps/latest_activity.txt` (which only lists what the firmware considers
     pending, leaving everything older invisible).
  2. What the unexplained bytes 16..27 of a 0x51 directory entry are. The spec had them down as
     "mtime and/or reserved, not confirmed which". This correlates them against a file's real
     size, which is known independently from the 0x11 read header -- if a field matches, that
     settles it. (Answer: mtime in microseconds, then size as a uint32.)
  3. What `adaf0001` holds. It is the one FTS characteristic marked [read] that nothing has ever
     touched.

Usage:
    python3 -u una_fts_walk.py <device-address> [--root /] [--depth 4] [--max-entries 400]
"""
import argparse
import asyncio
import os
import struct
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

FTS_CHAR_UUID = "adaf0002-4669-6c65-5472-616e73666572"
FTS_VERSION_CHAR_UUID = "adaf0001-4669-6c65-5472-616e73666572"

CMD_LIST = 0x50
RESP_LIST_ENTRY = 0x51
CMD_READ = 0x10
RESP_READ_CHUNK = 0x11

LIST_ENTRY_HEADER = 28
READ_CHUNK_HEADER = 16


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(destination="org.bluez", path=path, interface=iface,
                  member=member, signature=signature, body=body or [])
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


async def find_chars(bus, device_address):
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
        raise RuntimeError(f"No known BlueZ device with Address {device_address}")

    found = {}
    for path, ifaces in objects.items():
        if not path.startswith(device_path):
            continue
        char = ifaces.get("org.bluez.GattCharacteristic1")
        if not char:
            continue
        u = char["UUID"].value.lower()
        if u in (FTS_CHAR_UUID, FTS_VERSION_CHAR_UUID):
            found[u] = path
    return device_path, found


async def write_command(bus, char_path, data):
    await call(bus, char_path, "org.bluez.GattCharacteristic1", "WriteValue",
               "aya{sv}", [bytearray(data), {"type": Variant("s", "command")}])


class NotifyStream:
    """AcquireNotify's raw socket -- NOT PropertiesChanged, which coalesces rapid notifications
    and silently drops directory entries mid-listing."""

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
        while not self.queue.empty():
            self.queue.get_nowait()

    async def get(self, timeout):
        return await asyncio.wait_for(self.queue.get(), timeout=timeout)


def parse_list_entry(b):
    if len(b) < LIST_ENTRY_HEADER or b[0] != RESP_LIST_ENTRY:
        return None
    name_len, index, total, attr = struct.unpack("<HIII", b[2:16])
    if len(b) < LIST_ENTRY_HEADER + name_len:
        return None
    name = b[LIST_ENTRY_HEADER:LIST_ENTRY_HEADER + name_len].decode("ascii", "replace")
    return {
        "index": index, "total": total, "attr": attr, "name": name,
        "is_dir": bool(attr & 0x1),
        "unknown": b[16:28],          # the 12 bytes the spec cannot yet explain
        "raw": b,
    }


async def list_dir(stream, bus, char_path, path, timeout=6.0):
    """Returns (entries, ok). `ok` is False if the exchange did not complete cleanly.

    Strict request/response pairing matters more here than anywhere else in this protocol.
    There is no request ID in a 0x51 entry, so the ONLY thing tying a response to its request
    is that the previous exchange finished first. An earlier version of this walker kept
    waiting after an error reply, and the next directory's listing then swallowed the stragglers
    -- producing a tree that looked plausible but attributed files to the wrong directories
    (`/Apps/GlanceActivity/` appearing to contain `Cycling_1.3.0.uapp`). Hence: stop dead on a
    non-0x51 reply, and let the caller settle before the next request.
    """
    stream.drain()
    payload = bytes([CMD_LIST, 0]) + struct.pack("<H", len(path)) + path.encode("ascii")
    await write_command(bus, char_path, payload)

    entries, seen, total = [], set(), None
    while total is None or len(seen) < total:
        try:
            b = await stream.get(timeout)
        except asyncio.TimeoutError:
            print(f"      ** timeout listing {path} after {len(seen)} entr(ies); "
                  "attribution unreliable from here **")
            return entries, False
        e = parse_list_entry(b)
        if e is None:
            # 50 03 and friends: the firmware's own error reply for this path. It terminates
            # the exchange, so returning now is what keeps the next listing honest.
            print(f"      (error reply listing {path}: {b.hex()})")
            return entries, False
        total = e["total"]
        if e["index"] in seen:
            continue
        seen.add(e["index"])
        if e["name"]:
            entries.append(e)
    return entries, True


async def read_size(stream, bus, char_path, path, timeout=6.0):
    """One minimal 0x10 chunk, purely to learn total_size from the 0x11 header."""
    stream.drain()
    payload = (bytes([CMD_READ, 0]) + struct.pack("<H", len(path))
               + struct.pack("<I", 0) + struct.pack("<I", 16) + path.encode("ascii"))
    await write_command(bus, char_path, payload)
    try:
        b = await stream.get(timeout)
    except asyncio.TimeoutError:
        return None, None
    if len(b) < READ_CHUNK_HEADER or b[0] != RESP_READ_CHUNK:
        return None, b
    offset, total, chunklen = struct.unpack("<III", b[4:16])
    return total, b


async def walk(stream, bus, char_path, path, depth, max_depth, budget, files):
    if depth > max_depth or budget["n"] <= 0:
        return
    entries, ok = await list_dir(stream, bus, char_path, path)
    budget["n"] -= 1
    # Let the link go quiet before the next request, so a straggler cannot land in it.
    await asyncio.sleep(0.2)
    stream.drain()
    indent = "  " * depth
    print(f"{indent}{path}   ({len(entries)} entries){'' if ok else '  [INCOMPLETE]'}")
    for e in sorted(entries, key=lambda x: x["index"]):
        kind = "d" if e["is_dir"] else "-"
        print(f"{indent}  {kind} {e['name']:<34} attr=0x{e['attr']:08x} unk={e['unknown'].hex()}")
        if not e["is_dir"]:
            files.append((path.rstrip("/") + "/" + e["name"], e))
    for e in sorted(entries, key=lambda x: x["index"]):
        if e["is_dir"] and e["name"] not in (".", ".."):
            child = path.rstrip("/") + "/" + e["name"] + "/"
            await walk(stream, bus, char_path, child, depth + 1, max_depth, budget, files)


async def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address")
    parser.add_argument("--root", default="/")
    parser.add_argument("--depth", type=int, default=4)
    parser.add_argument("--max-listings", type=int, default=60,
                        help="cap on directory listings, so a deep tree cannot run away")
    args = parser.parse_args()

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, chars = await find_chars(bus, args.address)
    char_path = chars.get(FTS_CHAR_UUID)
    if char_path is None:
        raise RuntimeError("FTS characteristic not found -- is the watch connected?")
    print(f"device: {device_path}")

    version_path = chars.get(FTS_VERSION_CHAR_UUID)
    if version_path:
        print("\n=== adaf0001 (the never-read FTS [read] characteristic) ===")
        try:
            reply = await call(bus, version_path, "org.bluez.GattCharacteristic1",
                               "ReadValue", "a{sv}", [{}])
            raw = bytes(reply.body[0])
            printable = raw.decode("ascii", "replace")
            print(f"  {len(raw)} bytes: {raw.hex()}   ascii={printable!r}")
        except RuntimeError as e:
            print(f"  read failed: {e}")

    files = []
    async with NotifyStream(bus, char_path) as stream:
        print(f"\nnotify MTU: {stream.mtu}")
        print(f"\n=== filesystem walk from {args.root} (depth {args.depth}) ===")
        await walk(stream, bus, char_path, args.root, 0, args.depth,
                   {"n": args.max_listings}, files)

        print(f"\n=== 0x51 unknown-bytes correlation ({len(files)} file(s) seen) ===")
        print("Reading each candidate's real size from the 0x11 header, then checking whether")
        print("any field in the entry's unknown bytes 16..27 matches it.")
        print("Candidates are the largest and smallest files seen: the largest also tests")
        print("whether the 0x11 header's total_size really is capped at 65535.\n")

        def declared(entry):
            return struct.unpack_from("<I", entry["unknown"], 8)[0]

        by_size = sorted(files, key=lambda f: declared(f[1]))
        candidates, seen_paths = [], set()
        for cand in by_size[-4:][::-1] + by_size[:2]:
            if cand[0] not in seen_paths:
                seen_paths.add(cand[0])
                candidates.append(cand)
        for path, entry in candidates:
            total, raw = await read_size(stream, bus, char_path, path)
            unk = entry["unknown"]
            print(f"  {path}")
            print(f"    unknown bytes = {unk.hex()}")
            if total is None:
                print(f"    read gave no size (reply: {raw.hex() if raw else 'none'})")
                continue
            print(f"    real size     = {total}")
            hits = []
            for off in range(0, len(unk) - 1):
                if struct.unpack_from("<H", unk, off)[0] == total:
                    hits.append(f"u16LE@{off}")
            for off in range(0, len(unk) - 3):
                if struct.unpack_from("<I", unk, off)[0] == total:
                    hits.append(f"u32LE@{off}")
            print(f"    size found in unknown bytes at: {', '.join(hits) if hits else 'NOWHERE'}")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (RuntimeError, KeyboardInterrupt) as e:
        print(f"\nerror: {e}", file=sys.stderr)
        sys.exit(1)
