#!/usr/bin/env python3
"""
Standalone, phone-free BLE client for the UNA Watch's File Transfer Service (FTS).

Validated against a real watch: lists directories, pulls .fit activity files with a correct
FIT header and matching CRC-16, and writes/reads back arbitrary files up to 29 MiB byte-exact.
See ../BLE-COMPANION-protocol-spec.md for the full protocol writeup (§2.2 read+write framing,
§4 auth, §6a prototype notes) and ../../2026-08-07-ble-write-path/README.md for the write-path
throughput measurements and failure-mode findings this client was extended to produce.

Requirements:
    - Linux with BlueZ (system D-Bus), a Bluetooth adapter, and `dbus_fast` installed
      (pip install dbus_fast).
    - The watch must already be paired/bonded via `bluetoothctl` first (this script does not
      do pairing itself - BlueZ's SMP passkey agent flow is easiest driven interactively).
    - The watch's advertising window is short; if commands here time out, wake the watch
      (raise wrist / press a button) and retry. auto_connect_pull.py / auto_pair.py in this
      same directory handle the event-driven connect/pair dance more reliably than repeated
      one-shot `bluetoothctl connect` calls.

Usage:
    python3 una_ble_client.py <device-address> list <path>
    python3 una_ble_client.py <device-address> read <path> [output-file]
    python3 una_ble_client.py <device-address> write <local-file> <remote-path>

Example:
    python3 una_ble_client.py E8:DF:D5:49:4C:40 list /Apps/
    python3 una_ble_client.py E8:DF:D5:49:4C:40 read \\
        /Apps/GpsLab/ActivityArchive/202607/activity_20260730T230037.fit out.fit

write's <remote-path> must fall under WRITE_PATH_ALLOWLIST_PREFIXES below -- this is a hard
safety allowlist, not a suggestion (see BLE-COMPANION-write-path-prompt.md §0.1): writing to an
unrecognised path, especially 0:/ble.ota (where firmware OTA images stage), risks bricking the
watch.
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


async def read_file(bus, char_path: str, filepath: str, chunk_len: int = 128, mtu_out: dict = None):
    """FTS whole-file read: 0x10 request per chunk (offset advancing), 0x11 response per chunk.

    NOTE: a single 0x10 request does NOT stream the whole *file* -- it delivers at most the
    requested chunk_len and then stops, so one request per chunk is required. See spec doc §6a
    for the full story; this differs from what an earlier phone-capture analysis assumed.
    (Passing a chunk_len >= the file size does pull the whole file in one request on 1.4.0, but
    that is the chunk_len window happening to cover the file, not whole-file streaming.)

    offset/total/chunklen are genuine 32-bit fields (matching Adafruit's real BLE File Transfer
    Service spec byte-for-byte -- see spec doc §1.2/§2.2). An earlier version of this function
    parsed them as 16-bit and silently discarded the high half, which produced a false "64KB
    ceiling" finding on a real >64KB file -- retracted once a live phone-sync capture showed the
    high 16 bits were non-zero and combined with the low 16 bits into the correct true size.

    FRAMING (retested on firmware 1.4.0, 2026-08-18 -- see ../../2026-08-18-fts-read-chunklen-fix/):
    one 0x10 request is answered by one or MORE 0x11 notifications, and every one of them carries
    its own 16-byte header whose offset/real_chunklen describe just that notification. A single
    notification tops out at MTU-16-3 payload bytes (201 at MTU=220), so any chunk_len above that
    simply arrives as several framed notifications. The loop below accumulates whole framed
    notifications until the requested chunk_len (or EOF) is satisfied.

    An earlier version of this docstring described continuation notifications as headerless raw
    payload appended to the first one. That was never actually observed -- it was a hypothesis
    left over from the 1.3.0 firmware, which sent no continuations at all (see below).

    IMPORTANT: the loop advances `offset` by the bytes ACTUALLY delivered, never by the
    advertised real_chunklen. On firmware 1.3.0 those two could disagree -- real_chunklen was
    clamped to MTU-16 but only MTU-16-3 bytes were ever sent, with no continuation, so a client
    trusting the header hung for any chunk_len above MTU-19 (upstream issue #272, fixed in 1.4.0).
    Trusting delivered bytes instead keeps this function correct on both firmwares; on 1.3.0 an
    oversized chunk_len merely costs one 8s stall per chunk rather than hanging outright.

    mtu_out, if given, is filled in with {'mtu': <negotiated MTU>} once AcquireNotify returns.
    """
    path_b = filepath.encode("ascii")
    chunks = {}
    total_size = None

    async def request_chunk(bus_, offset):
        cmd = (b"\x10\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", offset)
               + struct.pack("<I", chunk_len) + path_b)
        await write_command(bus_, char_path, cmd)

    async with NotifyStream(bus, char_path) as stream:
        if mtu_out is not None:
            mtu_out["mtu"] = stream.mtu
        offset = 0
        while True:
            await request_chunk(bus, offset)
            got = 0  # bytes actually delivered for THIS request, across all its notifications
            while True:
                try:
                    b = await stream.get(timeout=8.0)
                except asyncio.TimeoutError:
                    break
                if not b or b[0] != 0x11 or len(b) < 16:
                    break
                got_offset, total, real_chunklen = struct.unpack("<III", b[4:16])
                # Trust the bytes present, not the advertised length (see docstring re: #272).
                payload = b[16:16 + real_chunklen]
                total_size = total
                chunks[got_offset] = payload[:max(0, total - got_offset)]
                got += len(payload)
                if got_offset + len(payload) >= total or got >= chunk_len:
                    break
            if got == 0:
                break  # nothing delivered; bail rather than spin re-requesting the same offset
            offset += got
            if total_size is not None and offset >= total_size:
                break

    if total_size is None:
        return None
    buf = bytearray(total_size)
    for off, payload in chunks.items():
        buf[off:off + len(payload)] = payload
    return bytes(buf)


# Hard allowlist of write-target path prefixes -- see BLE-COMPANION-write-path-prompt.md §0.1.
# 0:/ble.ota stages firmware OTA images and a firmwareUpdateHandler very likely acts on
# whatever is there; writing to any path outside this allowlist is a hard error, not a warning.
# HelloWorld is a pre-existing throwaway dev/test app directory nothing else reads from (see
# memory: project_build_deploy_apps used the same app for USB-side scratch probe files).
WRITE_PATH_ALLOWLIST_PREFIXES = ("/Apps/HelloWorld/ble_bench_",)


def check_write_path_allowed(filepath: str):
    if not any(filepath.startswith(p) for p in WRITE_PATH_ALLOWLIST_PREFIXES):
        raise ValueError(
            f"refusing to write to {filepath!r}: not under an allowlisted prefix "
            f"{WRITE_PATH_ALLOWLIST_PREFIXES}. Extend WRITE_PATH_ALLOWLIST_PREFIXES deliberately "
            "if you really mean to write elsewhere -- never write to 0:/ble.ota or any path "
            "holding real data."
        )


async def write_file(bus, char_path: str, filepath: str, data: bytes, mtime_ns: int = 0,
                      progress=None):
    """FTS whole-file write, opcodes 0x20 (start) / 0x21 (device pacing ack) / 0x22 (data).

    HYPOTHESIS, not yet independently on-wire confirmed the way the read path was: this
    device's read (0x10/0x11) and listdir (0x50/0x51) paths are byte-exact structural clones of
    Adafruit's real BLE File Transfer Service wire format (see spec doc §1.2/§6a), and Adafruit's
    published reference client uses this exact opcode numbering and struct layout for WRITE, so
    it's the strongest available starting guess -- but per the write-path prompt's premise trap,
    "the read path is not the write path" and this has not been confirmed against a live capture
    the way 0x10/0x11 was. Treat any write result as PLAUSIBLE until cross-checked.

      Request  (Write Command) 0x20 start : struct.pack("<BxHIQI", 0x20, len(path), offset=0,
                mtime_ns, total_length) + path
      Response (Notification)  0x21 pacing: struct.unpack("<BBxxIQI", buf) ->
                cmd, status, current_offset, reserved, free_space
      Request  (Write Command) 0x22 data  : struct.pack("<BBxxII", 0x22, status, current_offset,
                free_space) + up to free_space bytes of file data

    The client sends at most `free_space` bytes per 0x22, then waits for the next 0x21 pacing
    response before sending more. status is Adafruit's OK=0x01/ERROR=0x02/ERROR_NO_FILE=0x03/
    ERROR_PROTOCOL=0x04 by the same hypothesis -- CONFIRMED for 0x04 (ERROR_PROTOCOL): probing
    a fresh path's 0x20 start with a nonzero offset gets rejected with exactly a 2-byte
    `[0x20, 0x04]` reply (the same 2-byte error envelope already documented for the read path's
    malformed-request case), matching writeHandler's own "Invalid offset..." log string.

    CONFIRMED (byte-exact round trip on real hardware) but with one real correction to the naive
    Adafruit reading: **current_offset in the 0x21 ack is not a continuation cursor on this
    firmware -- it reads back as 0 on every ack, mid-transfer and final alike.** An earlier
    version of this function trusted it to pick the next send position and looped forever,
    silently resending the first chunk on every 90ms round trip (caught by adding a progress
    callback and watching it print bytes_sent=0 hundreds of times in a row). The client's own
    running byte count is the only correct position tracker for both progress and completion;
    current_offset is not used for anything by this implementation.

    Refuses to run against any path outside WRITE_PATH_ALLOWLIST_PREFIXES -- see § 0.1 of the
    write-path investigation prompt.
    """
    check_write_path_allowed(filepath)
    path_b = filepath.encode("ascii")
    total_length = len(data)

    async with NotifyStream(bus, char_path) as stream:
        # free_space (below) turns out to just echo bytes-remaining, not an MTU-aware buffer
        # hint (CONFIRMED: a 1000-byte start-write got free_space=1000 back, same as
        # total_length) -- unlike Adafruit's real semantics where it bounds one safe write. The
        # client must independently cap each 0x22's payload to what fits in a single ATT Write
        # Command: 12-byte 0x22 header + data <= ATT_MTU - 3 (opcode+handle overhead).
        max_att_chunk = stream.mtu - 3 - 12
        start_cmd = (b"\x20\x00" + struct.pack("<H", len(path_b)) + struct.pack("<I", 0)
                     + struct.pack("<Q", mtime_ns) + struct.pack("<I", total_length) + path_b)
        # The underlying D-Bus WriteValue call has no timeout of its own -- if bluetoothd's
        # write ever stalls server-side (observed once: a 1000-byte multi-chunk write hung
        # indefinitely with no exception, no output, connection otherwise still alive), an
        # un-timed-out await here would just hang the whole script forever. Bound it.
        await asyncio.wait_for(write_command(bus, char_path, start_cmd), timeout=10.0)
        b = await stream.get(timeout=8.0)
        if not b or b[0] != 0x21 or len(b) < 20:
            raise RuntimeError(f"unexpected response to WRITE start: {b.hex() if b else None}")
        _, status, device_offset, _reserved, free_space = struct.unpack("<BBxxIQI", b[:20])
        if status != 0x01:
            raise RuntimeError(f"WRITE start rejected, status=0x{status:02x}, "
                                f"offset={device_offset}, free_space={free_space}")

        bytes_sent = device_offset  # normally 0 for a fresh write; nonzero would be a resume
        while bytes_sent < total_length:
            if free_space <= 0:
                raise RuntimeError(f"stalled: free_space={free_space} at bytes_sent={bytes_sent}")
            n = min(free_space, total_length - bytes_sent, max_att_chunk)
            chunk = data[bytes_sent:bytes_sent + n]
            # The 4th field is validated against the attached payload length, not free_space
            # verbatim (CONFIRMED: echoing a stale/larger free_space than bytes actually sent
            # gets a 2-byte [0x22, 0x04 ERROR_PROTOCOL] rejection) -- declare len(chunk).
            data_cmd = (b"\x22" + bytes([status]) + b"\x00\x00"
                        + struct.pack("<I", bytes_sent) + struct.pack("<I", len(chunk))
                        + chunk)
            await asyncio.wait_for(write_command(bus, char_path, data_cmd), timeout=10.0)
            b = await stream.get(timeout=8.0)
            if not b or b[0] != 0x21 or len(b) < 20:
                raise RuntimeError(f"unexpected pacing response at bytes_sent={bytes_sent}: "
                                    f"{b.hex() if b else None}")
            _, status, device_offset, _reserved, free_space = struct.unpack("<BBxxIQI", b[:20])
            if status != 0x01:
                raise RuntimeError(f"write rejected at bytes_sent={bytes_sent}, "
                                    f"status=0x{status:02x}")
            # CONFIRMED (the hard way -- an earlier version of this loop trusted device_offset
            # here and looped forever): device_offset in the 0x21 ack is NOT a continuation
            # cursor on this firmware. It reads back as 0 on every ack, mid-transfer and final
            # alike, not just the completion sentinel case. The client's own running byte count
            # is the only correct position tracker; device_offset is not used for anything.
            bytes_sent += n
            if progress is not None:
                progress(bytes_sent, total_length)
    return bytes_sent


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
    elif op == "write":
        # path here is the LOCAL source file; the remote destination is argv[4], and must fall
        # under WRITE_PATH_ALLOWLIST_PREFIXES -- see write_file()'s docstring and §0.1 of the
        # write-path investigation prompt for why this is a hard allowlist, not a warning.
        if len(sys.argv) < 5:
            print("write requires a remote destination: write <local-file> <remote-path>")
            sys.exit(1)
        remote_path = sys.argv[4]
        with open(path, "rb") as f:
            data = f.read()
        final_offset = await write_file(bus, char_path, remote_path, data)
        print(f"wrote {final_offset} bytes to {remote_path}")
    else:
        print(f"unknown operation: {op!r} (expected 'list', 'read', or 'write')")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
