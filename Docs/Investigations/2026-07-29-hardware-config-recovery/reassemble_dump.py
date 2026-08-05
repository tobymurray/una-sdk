#!/usr/bin/env python3
"""Reassemble the chunked flash dump from Apps/HelloWorld/ on the watch into a single
.bin, independently re-verifying every per-chunk CRC32 and the whole-image CRC32 against
what the device itself reported in dump_manifest.txt (on-device CRC vs host-recomputed
CRC is the two-independent-methods corroboration for the dump).

Usage: reassemble_dump.py <dir-containing-dump_manifest.txt-and-dump_*.bin> [-o out.bin]
"""
import argparse
import re
import sys
import zlib
from pathlib import Path

MANIFEST_HEADER_RE = re.compile(
    r"DUMP base=([0-9A-Fa-f]+) size=([0-9A-Fa-f]+) chunk=([0-9A-Fa-f]+) "
    r"subwrite=([0-9A-Fa-f]+) nchunks=(\d+)"
)
CHUNK_RE = re.compile(
    r"DUMP chunk=(\d+)/(\d+) off=([0-9A-Fa-f]+) size=([0-9A-Fa-f]+) "
    r"crc32=([0-9A-Fa-f]+) bw=(\d+) ok=([YN])"
)
WHOLE_RE = re.compile(r"DUMP whole_image_crc32=([0-9A-Fa-f]+)")
SPOT_RE = re.compile(r"DUMP spot addr=([0-9A-Fa-f]+) bytes=([0-9A-Fa-f]*)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=Path("flash_dump.bin"))
    args = ap.parse_args()

    manifest_path = args.dir / "dump_manifest.txt"
    if not manifest_path.exists():
        sys.exit(f"no dump_manifest.txt in {args.dir}")
    text = manifest_path.read_text(errors="replace")

    header = MANIFEST_HEADER_RE.search(text)
    if not header:
        sys.exit("manifest header line not found - was the dump barely started?")
    base, size, chunk, subwrite, nchunks = header.groups()
    base, size, chunk, nchunks = int(base, 16), int(size, 16), int(chunk, 16), int(nchunks)
    print(f"base=0x{base:08X} size=0x{size:X} chunk=0x{chunk:X} nchunks={nchunks}")

    chunks = {int(m.group(1)): m for m in CHUNK_RE.finditer(text)}
    print(f"manifest describes {len(chunks)}/{nchunks} chunks")
    if len(chunks) < nchunks:
        print(f"** INCOMPLETE DUMP: only {len(chunks)}/{nchunks} chunks reached the "
              "manifest - reconnect happened before the app finished. Missing chunks "
              "will be zero-filled in the output; re-run the app to fill the rest, "
              "using a separate sweep round that starts from the first missing chunk.")

    out_buf = bytearray(size)
    bad = []
    for i in range(nchunks):
        m = chunks.get(i)
        if m is None:
            bad.append((i, "missing from manifest"))
            continue
        off = int(m.group(3), 16)
        clen = int(m.group(4), 16)
        expected_crc = int(m.group(5), 16)
        bw = int(m.group(6))
        ok_flag = m.group(7)
        if ok_flag != "Y" or bw != clen:
            bad.append((i, f"device reported ok={ok_flag} bw={bw}/{clen}"))

        fname = args.dir / f"dump_{off:06X}.bin"
        if not fname.exists():
            bad.append((i, f"file {fname.name} not found on host"))
            continue
        data = fname.read_bytes()
        if len(data) != clen:
            bad.append((i, f"file size {len(data)} != expected {clen}"))
        actual_crc = zlib.crc32(data) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            bad.append((i, f"CRC32 mismatch: file={actual_crc:08X} manifest={expected_crc:08X}"))
        out_buf[off:off + len(data)] = data

    whole_m = WHOLE_RE.search(text)
    if whole_m and not bad:
        device_whole_crc = int(whole_m.group(1), 16)
        host_whole_crc = zlib.crc32(bytes(out_buf)) & 0xFFFFFFFF
        match = "MATCH" if device_whole_crc == host_whole_crc else "MISMATCH"
        print(f"whole-image CRC32: device={device_whole_crc:08X} host={host_whole_crc:08X} [{match}]")
    elif whole_m:
        print("whole-image CRC32 check skipped (chunk errors present, see above)")
    else:
        print("no whole_image_crc32 line found - dump may not have finished")

    for spot_addr, spot_bytes in SPOT_RE.findall(text):
        addr = int(spot_addr, 16)
        off = addr - base
        if 0 <= off < len(out_buf):
            actual = out_buf[off:off + len(spot_bytes) // 2].hex().upper()
            match = "MATCH" if actual == spot_bytes.upper() else "MISMATCH"
            print(f"spot addr=0x{addr:08X} manifest={spot_bytes.upper()} reassembled={actual} [{match}]")

    if bad:
        print(f"\n{len(bad)} problem(s):")
        for i, msg in bad:
            print(f"  chunk {i}: {msg}")
    else:
        print(f"\nAll {nchunks} chunks verified clean.")

    args.out.write_bytes(out_buf)
    print(f"wrote {args.out} ({len(out_buf)} bytes)")


if __name__ == "__main__":
    main()
