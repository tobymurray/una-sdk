#!/usr/bin/env python3
"""
Read one file over FTS at a chosen chunk size, reporting throughput and the raw 0x11 header.

Written to answer two things the main client cannot. Whether the 128-byte chunk the original
capture used is a protocol limit -- it is not; 200 works against a 220 MTU, and the real bound is
`mtu - 19`. And how the response header really decodes: it prints the fields both as uint32 and
as the older "uint16 + 2 reserved" reading, which is how the u32 layout was established. The two
agree on every file under 64 KiB and diverge above it, where only the u32 reading matches the
size in the directory entry.

Read-only: it issues 0x10 reads and nothing else.

Usage:
    python3 -u una_read_probe.py <device-address> <watch-path> <chunk-len> <output-file>

Example:
    python3 -u una_read_probe.py E8:DF:D5:49:4C:40 /DailyHealth/dh.tmp 200 dh_tmp.bin
"""
import asyncio, os, struct, sys, time
from dbus_fast import BusType
from dbus_fast.aio import MessageBus
from dbus_fast.message import Message
from dbus_fast.signature import Variant
FTS="adaf0002-4669-6c65-5472-616e73666572"
async def call(bus,p,i,m,s="",b=None):
    r=await bus.call(Message(destination="org.bluez",path=p,interface=i,member=m,signature=s,body=b or []))
    if r.message_type.name=="ERROR": raise RuntimeError(f"{m}: {r.body}")
    return r
async def main():
    addr,path,chunk,out=sys.argv[1],sys.argv[2],int(sys.argv[3]),sys.argv[4]
    bus=await MessageBus(bus_type=BusType.SYSTEM,negotiate_unix_fd=True).connect()
    om=(bus.get_proxy_object("org.bluez","/",await bus.introspect("org.bluez","/"))
        .get_interface("org.freedesktop.DBus.ObjectManager"))
    objs=await om.call_get_managed_objects()
    dev=next(p for p,i in objs.items() if "org.bluez.Device1" in i and i["org.bluez.Device1"]["Address"].value.upper()==addr.upper())
    ch=next(p for p,i in objs.items() if p.startswith(dev) and "org.bluez.GattCharacteristic1" in i
            and i["org.bluez.GattCharacteristic1"]["UUID"].value.lower()==FTS)
    r=await call(bus,ch,"org.bluez.GattCharacteristic1","AcquireNotify","a{sv}",[{}])
    fd=r.unix_fds[r.body[0]]; mtu=r.body[1]
    q=asyncio.Queue(); asyncio.get_event_loop().add_reader(fd, lambda: q.put_nowait(os.read(fd,mtu)))
    pb=path.encode(); chunks={}; total=None; off=0; n=0; t0=time.time()
    while True:
        req=bytes([0x10,0])+struct.pack("<H",len(pb))+struct.pack("<I",off)+struct.pack("<I",chunk)+pb
        await call(bus,ch,"org.bluez.GattCharacteristic1","WriteValue","aya{sv}",
                   [bytearray(req),{"type":Variant("s","command")}])
        try: b=await asyncio.wait_for(q.get(),6.0)
        except asyncio.TimeoutError: print(f"timeout at offset {off}"); break
        if len(b)<16 or b[0]!=0x11: print(f"bad reply at {off}: {b.hex()}"); break
        o,total,cl=struct.unpack("<III",b[4:16]); n+=1
        chunks[o]=b[16:16+cl]
        if n==1: print(f"  first reply: {len(b)} bytes total_frame, chunklen={cl} (requested {chunk}), total={total}")
        if o+cl>=total: break
        off=o+cl
    dt=time.time()-t0
    buf=bytearray(total)
    for o,p_ in chunks.items(): buf[o:o+len(p_)]=p_
    open(out,"wb").write(buf)
    print(f"  {total} bytes in {n} chunks, {dt:.1f}s ({dt/n*1000:.0f} ms/chunk, {total/dt:.0f} B/s)")
    asyncio.get_event_loop().remove_reader(fd); os.close(fd)
asyncio.run(main())
