#!/usr/bin/env python3
"""
Black-box probe for the UNA Watch's CANS notification service, against a watch you own.

The wire format was recovered by decompiling the vendor app (see ../BLE-COMPANION-protocol-spec.md
§3c). Everything there is read from the app's own encoders and none of it has been seen on the
air. This script is the missing half: it sends the frames that document claims to a real watch and
records exactly what comes back, so an implementation is built on observed behaviour rather than on
someone else's source.

Every question below is one an implementation has to answer, and none of them can be answered by
reading the app -- the app shows what it sends, not what the watch requires.

  Q1  Does the watch ask for attributes at all when it receives an event, and which ones, with
      what maxLength values?
  Q2  Does it ask immediately, or only when the wearer opens the notification? This decides
      whether a companion may forget a notification's text after sending the event, or must
      retain it indefinitely against a request that may never come.
  Q3  What happens if the request is never answered -- retry, timeout, or a stuck watch?
  Q4  Is maxLength a real constraint? Sending more than asked for is the obvious way for a
      naive implementation to break, and the app never does it.
  Q5  Does a response larger than one packet actually reassemble, given fragments carry no
      header and ordering is the only thing holding them together?
  Q6  Does Remove clear a notification, and does Modify update one in place?
  Q7  Does Category change what the watch asks for -- does a Call want the action labels?
  Q8  Do ExecutePositiveAction / ExecuteNegativeAction arrive when the wearer acts on the watch?
  Q9  Does the watch accept an error response gracefully, or does declining wedge it?
  Q10 Is UID 0 legal, and what happens when a UID is reused?

Safety: this writes only to the CANS characteristics, which is what sending a notification means.
It sends no FTS write/delete/move/mkdir, no `0x30`, and no CCS opcode at all -- CCS is subscribed
to passively, never written. Nothing on the watch's filesystem is touched.

Several phases need you to look at the watch and say what you see; the script prompts and waits.
Answers are recorded alongside the captured bytes, because for questions like Q6 the wire is
silent and the screen is the only evidence.

Requirements:
    - Linux with BlueZ, and `dbus_fast` (pip install dbus_fast).
    - The watch bonded via `bluetoothctl` -- this does not pair.
    - No phone connected: this firmware takes one central at a time.

Usage:
    python3 una_cans_probe.py <device-address> [--phases 1,2,3] [--log run.jsonl]
"""
import argparse
import asyncio
import datetime
import json
import os
import struct
import sys

from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant

CANS_NOTIFY_CHAR = "554e4100-28e7-4811-0001-141f8b92ee40"
CANS_COMMAND_CHAR = "554e4100-28e7-4811-0002-141f8b92ee40"
CCS_EVENT_CHAR = "554e4100-a2cf-4df8-0002-7e1e48595106"
DIS_FIRMWARE_CHAR = "00002a26-0000-1000-8000-00805f9b34fb"
FTS_VERSION_CHAR = "adaf0001-4669-6c65-5472-616e73666572"

ACTION_ADD, ACTION_REMOVE, ACTION_MODIFY = 0, 1, 2
CATEGORY_OTHER, CATEGORY_MESSAGE, CATEGORY_CALL = 0, 1, 2

CMD_REQUEST_ATTRIBUTES = 0x03
CMD_EXECUTE_POSITIVE = 0x04
CMD_EXECUTE_NEGATIVE = 0x05

ATTRIBUTE_NAMES = {
    1: "Title", 2: "Subtitle", 3: "MessageContentSize", 4: "Message",
    5: "AppIdentifier", 6: "AppName", 7: "Timestamp",
    8: "PositiveActionLabel", 9: "NegativeActionLabel",
}
ERROR_NAMES = {128: "InvalidRequestFormat", 129: "NotificationUIDNotFound",
               130: "AttributeDataNotAvailable"}
ACTION_NAMES = {0: "Add", 1: "Remove", 2: "Modify"}
CATEGORY_NAMES = {0: "Other", 1: "Message", 2: "Call"}


class Recorder:
    """Every frame and every human observation, in order, to one JSONL file. The transcript is
    the deliverable -- a conclusion nobody can trace back to bytes is not worth much."""

    def __init__(self, path):
        self.path = path
        self.fh = open(path, "a") if path else None
        self.events = []

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
    manager = obj.get_interface("org.freedesktop.DBus.ObjectManager")
    return await manager.call_get_managed_objects()


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
            return path, list(char["Flags"].value) if "Flags" in char else []
    return None, []


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
                raise RuntimeError(
                    f"{e}\n  Every connect attempt aborted. The watch must be awake and no phone\n"
                    "  may hold the connection -- this firmware takes one central at a time.")
            print(f"    {e}; retrying in {delay:.0f}s")
            await asyncio.sleep(delay)


async def write_command(bus, char_path, data):
    await call(bus, char_path, "org.bluez.GattCharacteristic1", "WriteValue",
               "aya{sv}", [bytearray(data), {"type": Variant("s", "command")}])


async def read_value(bus, char_path):
    reply = await call(bus, char_path, "org.bluez.GattCharacteristic1", "ReadValue",
                       "a{sv}", [{}])
    return bytes(reply.body[0])


class NotifyStream:
    """AcquireNotify's raw socket rather than D-Bus PropertiesChanged, which was found to
    silently coalesce rapid successive notifications -- fatal for a probe whose whole job is
    counting and timing frames."""

    def __init__(self, bus, char_path, label, recorder):
        self.bus, self.char_path = bus, char_path
        self.label, self.recorder = label, recorder
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
                # EOF: the link dropped. The fd stays readable forever in that state, so the
                # reader has to be torn down here or it spins, and a dropped connection has to be
                # visible as a result rather than as a silent absence of frames.
                self.closed = True
                asyncio.get_event_loop().remove_reader(self.fd)
                self.recorder.record("link_lost", char=self.label)
                return
            self.recorder.record("rx", char=self.label, hex=data.hex(),
                                 decoded=decode_inbound(data))
            self.queue.put_nowait(data)

        asyncio.get_event_loop().add_reader(self.fd, on_readable)
        return self

    async def __aexit__(self, *exc):
        if not self.closed:
            asyncio.get_event_loop().remove_reader(self.fd)
        os.close(self.fd)

    def drain(self):
        stale = []
        while not self.queue.empty():
            stale.append(self.queue.get_nowait())
        return stale

    async def get(self, timeout):
        return await asyncio.wait_for(self.queue.get(), timeout=timeout)

    async def collect(self, seconds):
        """Everything that arrives in a window, rather than the first frame. A watch that sends
        two frames where one was expected is a finding, and get() would hide it."""
        out = []
        deadline = asyncio.get_event_loop().time() + seconds
        while True:
            if self.closed:
                raise RuntimeError("link lost mid-collection -- reconnect and re-run")
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                return out
            try:
                out.append(await asyncio.wait_for(self.queue.get(), timeout=remaining))
            except asyncio.TimeoutError:
                return out


def build_event(uid, action, category):
    return bytes([0x01, action]) + struct.pack("<I", uid) + bytes([category])


def build_attribute_response(uid, attributes):
    """attributes: list of (attribute_id, value_bytes)."""
    out = bytes([CMD_REQUEST_ATTRIBUTES]) + struct.pack("<I", uid)
    for attribute_id, value in attributes:
        out += bytes([attribute_id]) + struct.pack("<H", len(value)) + value
    return out


def decode_inbound(data):
    """Decode a watch->phone frame per §3c.4, and say plainly when it does not fit, so a frame
    the recovered format does not explain is visible rather than silently mis-parsed."""
    if not data:
        return {"note": "empty frame"}
    cmd = data[0]
    if cmd == CMD_REQUEST_ATTRIBUTES:
        if len(data) < 5:
            return {"command": "RequestAttributes", "note": f"short: {len(data)} bytes"}
        uid = struct.unpack("<I", data[1:5])[0]
        requested, offset, trailing = [], 5, None
        while offset + 3 <= len(data):
            attribute_id = data[offset]
            max_length = struct.unpack("<H", data[offset + 1:offset + 3])[0]
            requested.append({"id": attribute_id,
                              "name": ATTRIBUTE_NAMES.get(attribute_id, f"unknown({attribute_id})"),
                              "maxLength": max_length})
            offset += 3
        if offset != len(data):
            trailing = data[offset:].hex()
        out = {"command": "RequestAttributes", "uid": uid, "requested": requested}
        if trailing:
            out["unparsed_trailing_bytes"] = trailing
        return out
    if cmd in (CMD_EXECUTE_POSITIVE, CMD_EXECUTE_NEGATIVE):
        name = "ExecutePositiveAction" if cmd == CMD_EXECUTE_POSITIVE else "ExecuteNegativeAction"
        if len(data) < 5:
            return {"command": name, "note": f"short: {len(data)} bytes"}
        out = {"command": name, "uid": struct.unpack("<I", data[1:5])[0]}
        if len(data) > 5:
            out["unparsed_trailing_bytes"] = data[5:].hex()
        return out
    return {"command": f"unrecognised(0x{cmd:02x})", "raw": data.hex()}


def summarise(frames):
    return [decode_inbound(f) for f in frames]


async def send_event(bus, chars, recorder, uid, action, category, note=""):
    frame = build_event(uid, action, category)
    recorder.record("tx", char="CANS-0001", hex=frame.hex(), note=note,
                    decoded={"uid": uid, "action": ACTION_NAMES[action],
                             "category": CATEGORY_NAMES[category]})
    print(f"  -> event uid={uid} {ACTION_NAMES[action]}/{CATEGORY_NAMES[category]}  {frame.hex()}")
    await write_command(bus, chars["cans_notify"], frame)


async def send_fragmented(bus, chars, recorder, payload, max_packet, note=""):
    """Slice exactly as the vendor app does: raw slices, no per-fragment header."""
    fragments = [payload[i:i + max_packet] for i in range(0, len(payload), max_packet)]
    recorder.record("tx", char="CANS-0002", note=note, total_bytes=len(payload),
                    fragments=len(fragments), max_packet=max_packet, hex=payload.hex())
    print(f"  -> response {len(payload)} bytes in {len(fragments)} fragment(s) of <={max_packet}")
    for fragment in fragments:
        await write_command(bus, chars["cans_command"], fragment)
    return len(fragments)


async def phase_inventory(bus, device_path, objects, chars, recorder, streams, args):
    """Free evidence: what the device says about itself before anything is sent to it.
    Also settles whether 1.4.0 reports FTS protocol 5, which nothing has yet checked on hardware."""
    print("\n== Phase 0: inventory ==")
    firmware_path, _ = find_char(objects, device_path, DIS_FIRMWARE_CHAR)
    firmware = (await read_value(bus, firmware_path)).decode(errors="replace") if firmware_path else None
    version_path, _ = find_char(objects, device_path, FTS_VERSION_CHAR)
    version_raw = await read_value(bus, version_path) if version_path else None
    version = struct.unpack("<I", version_raw)[0] if version_raw and len(version_raw) == 4 else None

    flags = {}
    for label, uuid in (("CANS-0001", CANS_NOTIFY_CHAR), ("CANS-0002", CANS_COMMAND_CHAR),
                        ("CCS-0002", CCS_EVENT_CHAR)):
        _, char_flags = find_char(objects, device_path, uuid)
        flags[label] = char_flags

    entry = recorder.record("inventory", firmware=firmware, fts_version=version,
                            fts_version_raw=version_raw.hex() if version_raw else None,
                            char_flags=flags, notify_mtu=streams["cans"].mtu)
    print(f"  firmware        {firmware}")
    print(f"  FTS version     {version}  (raw {version_raw.hex() if version_raw else '-'})")
    print(f"  notify MTU      {streams['cans'].mtu}")
    for label, char_flags in flags.items():
        print(f"  {label:10s}    {','.join(char_flags)}")
    return entry


async def phase_first_event(bus, chars, recorder, streams, args):
    """Q1/Q2/Q3. Send one Add and simply listen -- deliberately answering nothing, so the
    watch's unprompted behaviour is on record before any response influences it."""
    print("\n== Phase 1: does an event draw a request, and when? ==")
    print("  LOOK AT THE WATCH: does anything appear on screen?")
    streams["cans"].drain()
    uid = args.base_uid
    await send_event(bus, chars, recorder, uid, ACTION_ADD, CATEGORY_MESSAGE,
                     note="Q1/Q2: unanswered on purpose")

    immediate = await streams["cans"].collect(args.settle)
    print(f"  within {args.settle:.0f}s: {len(immediate)} frame(s)")
    for frame in immediate:
        print(f"     <- {frame.hex()}  {decode_inbound(frame)}")

    print(f"  now waiting {args.patience:.0f}s WITHOUT answering (Q3: does it retry or give up?)")
    print("  LOOK AT THE WATCH: open the notification list if nothing appeared.")
    later = await streams["cans"].collect(args.patience)
    print(f"  further {args.patience:.0f}s: {len(later)} frame(s)")
    for frame in later:
        print(f"     <- {frame.hex()}  {decode_inbound(frame)}")

    recorder.record("finding", question="Q1/Q2/Q3", uid=uid,
                    immediate=summarise(immediate), delayed=summarise(later),
                    immediate_count=len(immediate), delayed_count=len(later))
    return uid, immediate + later


async def phase_answer(bus, chars, recorder, streams, args, uid, requests):
    """Q4/Q5. Answer honestly first so the happy path is on record, then deliberately overrun
    maxLength on a second notification -- the mistake an implementation makes by accident."""
    print("\n== Phase 2: answering, truncation, fragmentation ==")
    max_packet = streams["cans"].mtu - 3

    request = next((decode_inbound(f) for f in requests
                    if decode_inbound(f).get("command") == "RequestAttributes"), None)
    if not request:
        print("  no RequestAttributes seen in phase 1; sending a fresh event to provoke one")
        streams["cans"].drain()
        uid = args.base_uid + 1
        await send_event(bus, chars, recorder, uid, ACTION_ADD, CATEGORY_MESSAGE)
        frames = await streams["cans"].collect(args.settle)
        request = next((decode_inbound(f) for f in frames
                        if decode_inbound(f).get("command") == "RequestAttributes"), None)
    if not request:
        print("  still nothing to answer -- skipping Q4/Q5, recorded as unreachable")
        recorder.record("finding", question="Q4/Q5", result="no RequestAttributes to answer")
        return uid

    print(f"  answering {request}")
    values = {1: b"Probe Title", 2: b"Subtitle", 4: b"Short body.",
              5: b"nodomain.freeyourgadget.gadgetbridge", 6: b"Gadgetbridge",
              7: datetime.datetime.now().strftime("%Y%m%dT%H%M%S").encode(),
              8: b"Open", 9: b"Dismiss"}
    attributes = []
    for requested in request["requested"]:
        attribute_id, limit = requested["id"], requested["maxLength"]
        if attribute_id == 3:
            attributes.append((3, struct.pack("<H", len(values.get(4, b"")))))
            continue
        value = values.get(attribute_id, b"")[:limit] if limit else values.get(attribute_id, b"")
        attributes.append((attribute_id, value))
    payload = build_attribute_response(request["uid"], attributes)
    fragments = await send_fragmented(bus, chars, recorder, payload, max_packet,
                                      note="Q4 honest answer, truncated to maxLength")
    after = await streams["cans"].collect(args.settle)
    print(f"  after answering: {len(after)} further frame(s)")
    print("  LOOK AT THE WATCH: is the notification readable, and is the text complete?")
    recorder.record("finding", question="Q4-honest", uid=request["uid"], fragments=fragments,
                    total_bytes=len(payload), followups=summarise(after))

    print(f"\n  now a long body to force fragmentation (Q5), max_packet={max_packet}")
    streams["cans"].drain()
    long_uid = args.base_uid + 10
    await send_event(bus, chars, recorder, long_uid, ACTION_ADD, CATEGORY_MESSAGE,
                     note="Q5 fragmentation")
    frames = await streams["cans"].collect(args.settle)
    long_request = next((decode_inbound(f) for f in frames
                         if decode_inbound(f).get("command") == "RequestAttributes"), None)
    if long_request:
        marker = ("The quick brown fox jumps over the lazy dog. " * 40).encode()
        limit = next((r["maxLength"] for r in long_request["requested"] if r["id"] == 4), 0)
        body = marker[:limit] if limit else marker
        long_attributes = [(1, b"Fragmented"), (4, body)]
        payload = build_attribute_response(long_request["uid"], long_attributes)
        fragments = await send_fragmented(bus, chars, recorder, payload, max_packet,
                                          note="Q5 fragmented answer")
        after = await streams["cans"].collect(args.settle)
        print(f"  {len(payload)} bytes in {fragments} fragments; {len(after)} further frame(s)")
        print("  LOOK AT THE WATCH: is the long body intact, truncated, or garbled?")
        recorder.record("finding", question="Q5", uid=long_request["uid"], fragments=fragments,
                        total_bytes=len(payload), body_bytes=len(body),
                        requested_max_length=limit, followups=summarise(after))
    else:
        recorder.record("finding", question="Q5", result="no RequestAttributes for long body")
    return long_uid


async def phase_lifecycle(bus, chars, recorder, streams, args):
    """Q6/Q7. Remove and Modify are silent on the wire, so the screen is the only evidence --
    the prompts are the measurement here, not decoration. Category is on the wire only insofar
    as it changes which attributes the watch asks for."""
    print("\n== Phase 3: Remove, Modify, Category ==")
    uid = args.base_uid + 20

    streams["cans"].drain()
    await send_event(bus, chars, recorder, uid, ACTION_ADD, CATEGORY_MESSAGE, note="Q6 setup")
    setup = await streams["cans"].collect(args.settle)
    await answer_if_asked(bus, chars, recorder, streams, setup, b"Lifecycle", b"Before modify.")

    print("  LOOK AT THE WATCH: note the notification, then watch it during Modify/Remove.")
    await asyncio.sleep(args.dwell)

    streams["cans"].drain()
    await send_event(bus, chars, recorder, uid, ACTION_MODIFY, CATEGORY_MESSAGE, note="Q6 modify")
    modify_frames = await streams["cans"].collect(args.settle)
    print(f"  Modify drew {len(modify_frames)} frame(s) "
          f"-- a re-request means content is re-fetched on change")
    await answer_if_asked(bus, chars, recorder, streams, modify_frames, b"Lifecycle", b"AFTER modify.")
    await asyncio.sleep(args.dwell)

    streams["cans"].drain()
    await send_event(bus, chars, recorder, uid, ACTION_REMOVE, CATEGORY_MESSAGE, note="Q6 remove")
    remove_frames = await streams["cans"].collect(args.settle)
    print(f"  Remove drew {len(remove_frames)} frame(s)")
    print("  LOOK AT THE WATCH: did it disappear?")
    recorder.record("finding", question="Q6", uid=uid,
                    modify=summarise(modify_frames), remove=summarise(remove_frames))
    await asyncio.sleep(args.dwell)

    for category, name in ((CATEGORY_CALL, "Call"), (CATEGORY_OTHER, "Other")):
        streams["cans"].drain()
        category_uid = args.base_uid + 30 + category
        await send_event(bus, chars, recorder, category_uid, ACTION_ADD, category,
                         note=f"Q7 category {name}")
        frames = await streams["cans"].collect(args.settle)
        decoded = summarise(frames)
        asked = [r["name"] for d in decoded for r in d.get("requested", [])]
        print(f"  category {name}: asked for {asked or 'nothing'}")
        await answer_if_asked(bus, chars, recorder, streams, frames, b"Category probe", b"Body.")
        recorder.record("finding", question="Q7", category=name, uid=category_uid,
                        requested=decoded, attributes_asked=asked)
        print(f"  LOOK AT THE WATCH: does a {name} look different from a Message?")
        await asyncio.sleep(args.dwell)
        streams["cans"].drain()
        await send_event(bus, chars, recorder, category_uid, ACTION_REMOVE, category,
                         note="tidy up")


async def answer_if_asked(bus, chars, recorder, streams, frames, title, body):
    """Answer a request if one is present, honouring maxLength. Used by phases whose subject is
    something else, so an unanswered request cannot be what makes them fail."""
    request = next((decode_inbound(f) for f in frames
                    if decode_inbound(f).get("command") == "RequestAttributes"), None)
    if not request:
        return False
    values = {1: title, 2: b"", 4: body, 5: b"probe", 6: b"Probe", 7: b"", 8: b"Yes", 9: b"No"}
    attributes = []
    for requested in request["requested"]:
        attribute_id, limit = requested["id"], requested["maxLength"]
        if attribute_id == 3:
            attributes.append((3, struct.pack("<H", len(body))))
            continue
        value = values.get(attribute_id, b"")
        attributes.append((attribute_id, value[:limit] if limit else value))
    payload = build_attribute_response(request["uid"], attributes)
    await send_fragmented(bus, chars, recorder, payload, streams["cans"].mtu - 3,
                          note="supporting answer")
    return True


async def phase_actions(bus, chars, recorder, streams, args):
    """Q8. Needs the wearer: nothing arrives unless someone presses something. A quiet window
    is a real result -- it would mean a companion cannot offer dismiss-from-watch."""
    print("\n== Phase 4: actions from the watch ==")
    uid = args.base_uid + 40
    streams["cans"].drain()
    await send_event(bus, chars, recorder, uid, ACTION_ADD, CATEGORY_MESSAGE, note="Q8 setup")
    frames = await streams["cans"].collect(args.settle)
    await answer_if_asked(bus, chars, recorder, streams, frames, b"Press me", b"Dismiss or open me.")
    print(f"  ACT ON THE WATCH NOW: open, dismiss, or use any button on this notification.")
    print(f"  listening {args.action_window:.0f}s")
    observed = await streams["cans"].collect(args.action_window)
    for frame in observed:
        print(f"     <- {frame.hex()}  {decode_inbound(frame)}")
    recorder.record("finding", question="Q8", uid=uid, observed=summarise(observed),
                    count=len(observed))
    print(f"  {len(observed)} frame(s) from the watch")


async def phase_errors(bus, chars, recorder, streams, args):
    """Q9/Q10. Declining a request, an unknown UID, UID 0, and UID reuse -- the states a real
    companion hits on its first restart, when the watch asks about something it no longer has."""
    print("\n== Phase 5: errors and UID edge cases ==")
    max_packet = streams["cans"].mtu - 3

    uid = args.base_uid + 50
    streams["cans"].drain()
    await send_event(bus, chars, recorder, uid, ACTION_ADD, CATEGORY_MESSAGE, note="Q9 setup")
    frames = await streams["cans"].collect(args.settle)
    if any(decode_inbound(f).get("command") == "RequestAttributes" for f in frames):
        error_frame = bytes([129])
        recorder.record("tx", char="CANS-0002", hex=error_frame.hex(),
                        note="Q9 NotificationUIDNotFound",
                        decoded={"errorCode": ERROR_NAMES[129]})
        print("  -> declining with NotificationUIDNotFound (129)")
        await write_command(bus, chars["cans_command"], error_frame)
        after = await streams["cans"].collect(args.patience)
        print(f"  after declining: {len(after)} frame(s) -- retries would show here")
        print("  LOOK AT THE WATCH: does it show an empty notification, an error, or hang?")
        recorder.record("finding", question="Q9", uid=uid, followups=summarise(after))
    else:
        recorder.record("finding", question="Q9", result="no request to decline")

    streams["cans"].drain()
    await send_event(bus, chars, recorder, 0, ACTION_ADD, CATEGORY_MESSAGE, note="Q10 uid=0")
    zero_frames = await streams["cans"].collect(args.settle)
    print(f"  uid=0 drew {len(zero_frames)} frame(s): {summarise(zero_frames)}")
    await answer_if_asked(bus, chars, recorder, streams, zero_frames, b"Zero UID", b"UID zero.")
    recorder.record("finding", question="Q10-zero", observed=summarise(zero_frames))

    streams["cans"].drain()
    reuse_uid = args.base_uid + 60
    await send_event(bus, chars, recorder, reuse_uid, ACTION_ADD, CATEGORY_MESSAGE, note="Q10 first")
    first = await streams["cans"].collect(args.settle)
    await answer_if_asked(bus, chars, recorder, streams, first, b"First", b"First body.")
    await asyncio.sleep(args.dwell)
    streams["cans"].drain()
    await send_event(bus, chars, recorder, reuse_uid, ACTION_ADD, CATEGORY_MESSAGE,
                     note="Q10 reuse same uid")
    second = await streams["cans"].collect(args.settle)
    await answer_if_asked(bus, chars, recorder, streams, second, b"Second", b"Second body.")
    print(f"  reused uid: first drew {len(first)}, second drew {len(second)} frame(s)")
    print("  LOOK AT THE WATCH: one notification or two?")
    recorder.record("finding", question="Q10-reuse", uid=reuse_uid,
                    first=summarise(first), second=summarise(second))


async def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address")
    parser.add_argument("--phases", default="0,1,2,3,4,5",
                        help="comma-separated phases to run (default all)")
    parser.add_argument("--log", default="cans_probe.jsonl")
    parser.add_argument("--base-uid", type=int, default=9000)
    parser.add_argument("--settle", type=float, default=6.0,
                        help="seconds to wait for a reply after sending")
    parser.add_argument("--patience", type=float, default=25.0,
                        help="seconds to wait when testing whether the watch retries or gives up")
    parser.add_argument("--dwell", type=float, default=8.0,
                        help="seconds to leave a notification on screen to be looked at")
    parser.add_argument("--action-window", type=float, default=45.0,
                        help="seconds to wait for the wearer to press something")
    parser.add_argument("--connect-retries", type=int, default=6)
    args = parser.parse_args()

    phases = {p.strip() for p in args.phases.split(",") if p.strip()}
    recorder = Recorder(args.log)
    recorder.record("run", address=args.address, phases=sorted(phases),
                    argv=sys.argv[1:])

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    device_path, objects = await find_device(bus, args.address)
    print(f"device {device_path}")
    await ensure_connected(bus, device_path, args.connect_retries)

    # Services can take a moment to resolve after connecting; re-read rather than trust the
    # snapshot taken before Connect().
    for _ in range(10):
        objects = await managed_objects(bus)
        if find_char(objects, device_path, CANS_COMMAND_CHAR)[0]:
            break
        await asyncio.sleep(1.0)

    chars = {}
    for key, uuid in (("cans_notify", CANS_NOTIFY_CHAR), ("cans_command", CANS_COMMAND_CHAR),
                      ("ccs_event", CCS_EVENT_CHAR)):
        path, _ = find_char(objects, device_path, uuid)
        chars[key] = path
        if path is None:
            print(f"  WARNING: {key} characteristic not found")
    if not chars["cans_notify"] or not chars["cans_command"]:
        recorder.record("abort", reason="CANS characteristics missing")
        raise SystemExit("CANS characteristics not present -- cannot probe")

    async with NotifyStream(bus, chars["cans_command"], "CANS-0002", recorder) as cans_stream:
        streams = {"cans": cans_stream}
        ccs_stream = None
        if chars["ccs_event"]:
            # Subscribed for the whole run and never written to. Anything that arrives is the
            # watch volunteering an event (activity-ended, find-phone), which no session has
            # yet observed -- free evidence for separate work.
            ccs_stream = NotifyStream(bus, chars["ccs_event"], "CCS-0002", recorder)
            await ccs_stream.__aenter__()
        try:
            if "0" in phases:
                await phase_inventory(bus, device_path, objects, chars, recorder, streams, args)
            requests, uid = [], args.base_uid
            if "1" in phases:
                uid, requests = await phase_first_event(bus, chars, recorder, streams, args)
            if "2" in phases:
                await phase_answer(bus, chars, recorder, streams, args, uid, requests)
            if "3" in phases:
                await phase_lifecycle(bus, chars, recorder, streams, args)
            if "4" in phases:
                await phase_actions(bus, chars, recorder, streams, args)
            if "5" in phases:
                await phase_errors(bus, chars, recorder, streams, args)
        finally:
            if ccs_stream:
                volunteered = ccs_stream.drain()
                recorder.record("finding", question="CCS-event-channel",
                                frames=[f.hex() for f in volunteered], count=len(volunteered))
                print(f"\n  CCS event characteristic: {len(volunteered)} unsolicited frame(s) "
                      f"during the whole run")
                for frame in volunteered:
                    print(f"     <- {frame.hex()}")
                await ccs_stream.__aexit__()

    print(f"\ntranscript: {args.log}  ({len(recorder.events)} entries)")
    recorder.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
