#!/usr/bin/env python3
"""
Probe for the FTS protocol-5 extensions and the questions left open around activity backfill,
against a watch you own. Read-only throughout.

UNA's published FTS document describes windowed reads, DIGEST and a fixed una-sdk#272, and
Gadgetbridge now implements windowing. But Android negotiated MTU 220 on the one phone this was
tested with, which is the same MTU the original 2026-08-07 measurements used, so two things stayed
untested: whether the notification size follows the MTU or is pinned near 201, and whether the
advertised chunk length stays honest at a larger MTU. BlueZ typically negotiates higher than
Android does, which is the whole reason to ask these here.

What this settles:

  V1  Does firmware 1.4.0 report FTS protocol 5 on `adaf0001`? Nothing has read it on hardware.
  V2  With a large window, do READ_DATA notifications actually arrive as a burst, how big is each
      one at this MTU, and does the advertised chunkLength match what the notification carries?
      A mismatch here at MTU > 220 would mean una-sdk#272 is fixed only for the MTU it was tested
      at, and Gadgetbridge's clamp is load-bearing rather than belt-and-braces.
  V3  Does DIGEST (0x70) work, and does its CRC-32 match the file actually read back? That
      validates DIGEST as a cheap integrity check and, at the same time, validates the read.
  V4  Is there a ceiling on the window size, and does asking for far more than 4096 help or hurt?
  V5  How much is actually in the activity archive -- file count and total bytes -- so backfill
      can be sized rather than guessed at.

Safety: sends only READ (0x10), READ_PACING (0x12), LISTDIR (0x50) and DIGEST (0x70). No write,
delete, move or mkdir, no `0x30`, and no CCS traffic at all.

Requirements: Linux with BlueZ, `dbus_fast`, watch bonded and no phone connected.

Usage:
    python3 una_v5_probe.py <device-address> [--log v5_probe.jsonl]
"""
import argparse
import asyncio
import datetime
import json
import os
import struct
import sys
import zlib

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

FTS_VERSION_CHAR = "adaf0001-4669-6c65-5472-616e73666572"
FTS_TRANSFER_CHAR = "adaf0002-4669-6c65-5472-616e73666572"

CMD_READ, RESP_READ_DATA, CMD_READ_PACING = 0x10, 0x11, 0x12
CMD_LIST_DIR, RESP_LIST_ENTRY = 0x50, 0x51
CMD_DIGEST, RESP_DIGEST = 0x70, 0x71

STATUS_NAMES = {1: "OK", 2: "ERROR", 3: "ERROR_NO_FILE", 4: "ERROR_PROTOCOL", 5: "ERROR_READ_ONLY"}
READ_HEADER = 16
LIST_HEADER = 28
MANIFEST_PATH = "/Apps/latest_activity.txt"


class Recorder:
    def __init__(self, path):
        self.path, self.fh, self.events = path, (open(path, "a") if path else None), []

    def record(self, kind, **fields):
        entry = {"t": datetime.datetime.now().isoformat(timespec="milliseconds"),
                 "kind": kind, **fields}
        self.events.append(entry)
        if self.fh:
            self.fh.write(json.dumps(entry) + "\n")
            self.fh.flush()
        return entry

    def close(self):
        if self.fh:
            self.fh.close()


async def call(bus, path, iface, member, signature="", body=None):
    msg = Message(destination="org.bluez", path=path, interface=iface,
                  member=member, signature=signature, body=body or [])
    reply = await bus.call(msg)
    if reply.message_type.name == "ERROR":
        raise RuntimeError(f"{member} failed: {reply.body}")
    return reply


async def managed_objects(bus):
    introspection = await bus.introspect("org.bluez", "/")
    obj = bus.get_proxy_object("org.bluez", "/", introspection)
    return await obj.get_interface("org.freedesktop.DBus.ObjectManager").call_get_managed_objects()


async def find_device(bus, address):
    objects = await managed_objects(bus)
    for path, ifaces in objects.items():
        dev = ifaces.get("org.bluez.Device1")
        if dev and dev["Address"].value.upper() == address.upper():
            return path, objects
    raise RuntimeError(f"No known BlueZ device with Address {address} -- pair it first")


def find_char(objects, device_path, uuid):
    for path, ifaces in objects.items():
        if not path.startswith(device_path):
            continue
        char = ifaces.get("org.bluez.GattCharacteristic1")
        if char and char["UUID"].value.lower() == uuid.lower():
            return path
    return None


async def ensure_connected(bus, device_path, retries=6, delay=8.0):
    for attempt in range(1, retries + 1):
        props = await call(bus, device_path, "org.freedesktop.DBus.Properties", "Get",
                           "ss", ["org.bluez.Device1", "Connected"])
        if props.body[0].value:
            return
        print(f"  connecting (attempt {attempt}/{retries})...")
        try:
            await call(bus, device_path, "org.bluez.Device1", "Connect")
            await asyncio.sleep(1.5)
            return
        except RuntimeError as e:
            if attempt == retries:
                raise RuntimeError(f"{e}\n  Watch must be awake with no phone connected.")
            await asyncio.sleep(delay)


async def write_command(bus, char_path, data):
    await call(bus, char_path, "org.bluez.GattCharacteristic1", "WriteValue",
               "aya{sv}", [bytearray(data), {"type": Variant("s", "command")}])


class NotifyStream:
    """Raw AcquireNotify socket. D-Bus PropertiesChanged coalesces rapid notifications, which
    would destroy exactly the burst structure V2 exists to measure."""

    def __init__(self, bus, char_path):
        self.bus, self.char_path = bus, char_path
        self.fd = self.mtu = None
        self.closed = False
        self.queue = asyncio.Queue()

    async def __aenter__(self):
        reply = await call(self.bus, self.char_path, "org.bluez.GattCharacteristic1",
                           "AcquireNotify", "a{sv}", [{}])
        fd_index, self.mtu = reply.body
        self.fd = reply.unix_fds[fd_index]

        def on_readable():
            try:
                data = os.read(self.fd, self.mtu)
            except (BlockingIOError, InterruptedError):
                return
            except OSError:
                data = b""
            if not data:
                self.closed = True
                asyncio.get_event_loop().remove_reader(self.fd)
                return
            self.queue.put_nowait((asyncio.get_event_loop().time(), data))

        asyncio.get_event_loop().add_reader(self.fd, on_readable)
        return self

    async def __aexit__(self, *exc):
        if not self.closed:
            asyncio.get_event_loop().remove_reader(self.fd)
        os.close(self.fd)

    def drain(self):
        while not self.queue.empty():
            self.queue.get_nowait()

    async def collect(self, seconds):
        out, deadline = [], asyncio.get_event_loop().time() + seconds
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                return out
            try:
                out.append(await asyncio.wait_for(self.queue.get(), timeout=remaining))
            except asyncio.TimeoutError:
                return out


def build_read(path, offset, chunk_len):
    encoded = path.encode()
    return (bytes([CMD_READ, 0]) + struct.pack("<H", len(encoded))
            + struct.pack("<II", offset, chunk_len) + encoded)


def build_pacing(offset, chunk_len):
    return bytes([CMD_READ_PACING, 1, 0, 0]) + struct.pack("<II", offset, chunk_len)


def build_listdir(path):
    encoded = path.encode()
    return bytes([CMD_LIST_DIR, 0]) + struct.pack("<H", len(encoded)) + encoded


def build_digest(path):
    encoded = path.encode()
    return bytes([CMD_DIGEST, 0]) + struct.pack("<H", len(encoded)) + encoded


def parse_read_data(data):
    if len(data) < READ_HEADER or data[0] != RESP_READ_DATA:
        return None
    offset, total, advertised = struct.unpack("<III", data[4:16])
    delivered = len(data) - READ_HEADER
    return {"status": data[1], "offset": offset, "total": total,
            "advertised": advertised, "delivered": delivered,
            "honest": advertised == delivered, "payload": data[READ_HEADER:]}


def parse_list_entry(data):
    if len(data) < LIST_HEADER or data[0] != RESP_LIST_ENTRY:
        return None
    name_len = struct.unpack("<H", data[2:4])[0]
    index, total, flags = struct.unpack("<III", data[4:16])
    mtime, size = struct.unpack("<QI", data[16:28])
    return {"index": index, "total": total, "is_dir": bool(flags & 1),
            "mtime": mtime, "size": size,
            "name": data[LIST_HEADER:LIST_HEADER + name_len].decode(errors="replace")}


async def read_file(bus, char, stream, recorder, path, window, settle=8.0, label=""):
    """Read a whole file with windowed pacing, recording the shape of every burst.

    Paces from the contiguous end and clamps each chunk to what its notification really carried,
    which is what Gadgetbridge does. Whether the clamp is ever load-bearing at this MTU is one of
    the things being measured, so mismatches are counted rather than quietly corrected.
    """
    stream.drain()
    chunks, bursts = {}, []
    contiguous, total, mismatches, requests = 0, None, 0, 0
    sizes = []

    await write_command(bus, char, build_read(path, 0, window))
    requests += 1
    started = asyncio.get_event_loop().time()

    while True:
        # Collect until the window is satisfied rather than until a timeout expires. Waiting out
        # `settle` after every window would make elapsed time a measure of this probe's patience
        # instead of the link's, which is the opposite of what V2 is for.
        window_end = contiguous + window if total is None else min(contiguous + window, total)
        frames = []
        deadline = asyncio.get_event_loop().time() + settle
        while True:
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break
            try:
                frames.append(await asyncio.wait_for(stream.queue.get(), timeout=remaining))
            except asyncio.TimeoutError:
                break
            held, total_seen = dict(chunks), total
            for _, raw in frames:
                parsed_frame = parse_read_data(raw)
                if parsed_frame:
                    held[parsed_frame["offset"]] = parsed_frame["payload"]
                    total_seen = parsed_frame["total"]
            walk = contiguous
            while walk in held:
                walk += len(held[walk])
            # The file's own length is only learned from the first reply, so a window larger than
            # the file would otherwise wait out the timeout and be scored as slow.
            if total_seen is not None and walk >= min(contiguous + window, total_seen):
                break
            if total_seen is None and walk >= window:
                break
        if not frames:
            break
        burst = []
        for arrival, data in frames:
            parsed = parse_read_data(data)
            if parsed is None:
                recorder.record("unexpected", label=label, hex=data.hex())
                continue
            if parsed["status"] != 1:
                recorder.record("read_status", label=label, path=path,
                                status=parsed["status"],
                                status_name=STATUS_NAMES.get(parsed["status"], "?"))
                return None, None
            total = parsed["total"]
            if not parsed["honest"]:
                mismatches += 1
            sizes.append(parsed["delivered"])
            chunks[parsed["offset"]] = parsed["payload"]
            burst.append({"offset": parsed["offset"], "advertised": parsed["advertised"],
                          "delivered": parsed["delivered"], "dt_ms": round((arrival - started) * 1000)})
        if burst:
            bursts.append(burst)
        while contiguous in chunks:
            contiguous += len(chunks[contiguous])
        if total is not None and contiguous >= total:
            break
        await write_command(bus, char, build_pacing(contiguous, window))
        requests += 1

    elapsed = asyncio.get_event_loop().time() - started
    if total is None:
        recorder.record("read_failed", label=label, path=path)
        return None, None
    data = bytearray(total)
    for offset, payload in chunks.items():
        usable = min(len(payload), total - offset)
        if usable > 0:
            data[offset:offset + usable] = payload[:usable]

    notifications = sum(len(b) for b in bursts)
    result = recorder.record(
        "read", label=label, path=path, window=window, total=total,
        notifications=notifications, requests=requests,
        notifications_per_request=round(notifications / requests, 2) if requests else 0,
        seconds=round(elapsed, 2), bytes_per_second=round(total / elapsed, 1) if elapsed else 0,
        header_mismatches=mismatches,
        notification_size_max=max(sizes) if sizes else 0,
        notification_size_min=min(sizes) if sizes else 0,
        burst_lengths=[len(b) for b in bursts], crc32=zlib.crc32(bytes(data)) & 0xFFFFFFFF)
    print(f"  {path} [{label}] window={window}: {total} bytes, {notifications} notifications / "
          f"{requests} requests, {elapsed:.1f}s ({total/elapsed if elapsed else 0:.0f} B/s), "
          f"sizes {min(sizes) if sizes else 0}-{max(sizes) if sizes else 0}, "
          f"header mismatches={mismatches}")
    return bytes(data), result


async def listdir(bus, char, stream, recorder, path, settle=8.0):
    stream.drain()
    await write_command(bus, char, build_listdir(path))
    entries, expected = [], None
    while True:
        frames = await stream.collect(settle)
        if not frames:
            break
        for _, data in frames:
            entry = parse_list_entry(data)
            if entry is None:
                recorder.record("unexpected", label="listdir", hex=data.hex())
                continue
            expected = entry["total"]
            if entry["name"]:
                entries.append(entry)
        if expected is not None and len(entries) >= expected:
            break
        if expected == 0:
            break
    recorder.record("listdir", path=path, count=len(entries), expected=expected,
                    entries=[{k: e[k] for k in ("name", "is_dir", "size")} for e in entries])
    return entries


async def walk_activity_archive(bus, char, stream, recorder, settle):
    """V5. Walks /Apps/<app>/Activity/<YYYYMM>/ and totals what a backfill would face. Read-only:
    LISTDIR only, no file is fetched here."""
    print("\n== V5: sizing the activity archive ==")
    files, apps = [], await listdir(bus, char, stream, recorder, "/Apps/", settle)
    for app in [a for a in apps if a["is_dir"]]:
        months = await listdir(bus, char, stream, recorder, f"/Apps/{app['name']}/Activity/", settle)
        for month in [m for m in months if m["is_dir"]]:
            base = f"/Apps/{app['name']}/Activity/{month['name']}/"
            for entry in await listdir(bus, char, stream, recorder, base, settle):
                if not entry["is_dir"]:
                    files.append({"path": base + entry["name"], "size": entry["size"]})
    fits = [f for f in files if f["path"].endswith(".fit")]
    total = sum(f["size"] for f in fits)
    recorder.record("finding", question="V5", app_count=len([a for a in apps if a["is_dir"]]),
                    file_count=len(files), fit_count=len(fits), fit_total_bytes=total,
                    largest=max((f["size"] for f in fits), default=0), files=files)
    print(f"  {len(fits)} .fit files across {len([a for a in apps if a['is_dir']])} apps, "
          f"{total} bytes total, largest {max((f['size'] for f in fits), default=0)}")
    return fits


async def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address")
    parser.add_argument("--log", default="v5_probe.jsonl")
    parser.add_argument("--settle", type=float, default=8.0)
    parser.add_argument("--windows", default="201,4096,16384",
                        help="window sizes to compare on one file (V2/V4)")
    parser.add_argument("--connect-retries", type=int, default=6)
    parser.add_argument("--target", help="skip the archive walk and read this path instead")
    parser.add_argument("--skip-walk", action="store_true")
    args = parser.parse_args()

    recorder = Recorder(args.log)
    recorder.record("run", address=args.address, argv=sys.argv[1:])
    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await find_device(bus, args.address)
    await ensure_connected(bus, device_path, args.connect_retries)
    for _ in range(10):
        objects = await managed_objects(bus)
        if find_char(objects, device_path, FTS_TRANSFER_CHAR):
            break
        await asyncio.sleep(1.0)

    version_path = find_char(objects, device_path, FTS_VERSION_CHAR)
    transfer_path = find_char(objects, device_path, FTS_TRANSFER_CHAR)
    if not transfer_path:
        raise SystemExit("FTS transfer characteristic not found")

    version_raw = bytes((await call(bus, version_path, "org.bluez.GattCharacteristic1",
                                    "ReadValue", "a{sv}", [{}])).body[0]) if version_path else b""
    version = struct.unpack("<I", version_raw)[0] if len(version_raw) == 4 else None
    print(f"\n== V1: FTS protocol version ==\n  adaf0001 = {version}  (raw {version_raw.hex()})")
    recorder.record("finding", question="V1", version=version, raw=version_raw.hex())

    async with NotifyStream(bus, transfer_path) as stream:
        print(f"  notify MTU {stream.mtu}  -> one notification can carry {stream.mtu - 3 - 16} "
              f"payload bytes if the firmware sizes to the MTU")
        recorder.record("mtu", notify_mtu=stream.mtu,
                        implied_payload=stream.mtu - 3 - READ_HEADER)

        fits = []
        if not (args.skip_walk or args.target):
            fits = await walk_activity_archive(bus, transfer_path, stream, recorder, args.settle)

        if args.target:
            target = {"path": args.target, "size": 0}
        else:
            target = max(fits, key=lambda f: f["size"]) if fits else None
        if target is None:
            print("\n  no .fit files found; falling back to the manifest for V2/V3")
            target = {"path": MANIFEST_PATH, "size": 0}

        print(f"\n== V2/V4: window sizes against {target['path']} ==")
        results, contents = [], {}
        for window in [int(w) for w in args.windows.split(",")]:
            data, result = await read_file(bus, transfer_path, stream, recorder,
                                           target["path"], window, args.settle,
                                           label=f"window={window}")
            if result:
                results.append(result)
                contents[window] = data
        identical = len({zlib.crc32(c) for c in contents.values()}) <= 1
        recorder.record("finding", question="V2/V4", results=results,
                        all_windows_agree=identical,
                        any_header_mismatch=any(r["header_mismatches"] for r in results))
        print(f"  every window returned identical bytes: {identical}")

        print(f"\n== V3: DIGEST ==")
        stream.drain()
        await write_command(bus, transfer_path, build_digest(target["path"]))
        frames = await stream.collect(args.settle)
        digest = None
        for _, data in frames:
            if len(data) >= 12 and data[0] == RESP_DIGEST:
                size, crc = struct.unpack("<II", data[4:12])
                digest = {"status": data[1], "status_name": STATUS_NAMES.get(data[1], "?"),
                          "size": size, "crc32": crc, "raw": data.hex()}
        local = None
        if contents:
            body = next(iter(contents.values()))
            local = {"size": len(body), "crc32": zlib.crc32(body) & 0xFFFFFFFF}
        agrees = bool(digest and local and digest["size"] == local["size"]
                      and digest["crc32"] == local["crc32"])
        recorder.record("finding", question="V3", digest=digest, locally_computed=local,
                        agrees=agrees, frames=[d.hex() for _, d in frames])
        if digest:
            print(f"  watch: size={digest['size']} crc32=0x{digest['crc32']:08x} "
                  f"status={digest['status_name']}")
        else:
            print(f"  no 0x71 response; frames: {[d.hex() for _, d in frames]}")
        if local:
            print(f"  local: size={local['size']} crc32=0x{local['crc32']:08x}")
        print(f"  agrees: {agrees}")

    print(f"\ntranscript: {args.log}  ({len(recorder.events)} entries)")
    recorder.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
