#!/usr/bin/env python3
"""Casual fuzzer for SDK::RawTiles::Container via the ASan rawtiles_check.

Phase 1: targeted structural mutations of stanley.rawtiles and golden-pyramid.
Phase 2: random 1-8 byte flips over golden fixtures, N iterations.

A run counts as a CRASH if:
  - the process died on a signal (negative returncode), or
  - returncode == 99 (ASAN_OPTIONS exitcode=99), or
  - output contains an AddressSanitizer / UBSan report.
Anything else (exit 0/1/2 with sane output) is a clean accept/reject.
Crashing inputs are saved to crashes/.
"""
import os
import random
import struct
import subprocess
import sys
from pathlib import Path

SP = Path("/private/tmp/claude-501/-Users-tobymurray-git-una-sdk/40e0097b-414e-41c2-a3e4-b98740c7a01b/scratchpad")
BIN = SP / "build" / "rawtiles_check_asan"
STANLEY = SP / "wt-rawtiles/Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles"
GOLDEN_DIR = SP / "rawtiles/spec/conformance/golden"
WORK = SP / "build" / "fuzz"
CRASH_DIR = WORK / "crashes"
WORK.mkdir(exist_ok=True)
CRASH_DIR.mkdir(exist_ok=True)

ENV = dict(os.environ, ASAN_OPTIONS="exitcode=99:abort_on_error=0",
           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

crashes = []
results = []


def run_case(tag: str, data: bytes) -> str:
    p = WORK / "case.rawtiles"
    p.write_bytes(data)
    try:
        r = subprocess.run([str(BIN), str(p)], capture_output=True, text=True,
                           timeout=30, env=ENV)
    except subprocess.TimeoutExpired:
        dest = CRASH_DIR / f"{tag}.rawtiles"
        dest.write_bytes(data)
        crashes.append((tag, "TIMEOUT", ""))
        return "TIMEOUT"
    out = r.stdout + r.stderr
    is_crash = (r.returncode < 0 or r.returncode == 99
                or "AddressSanitizer" in out or "runtime error:" in out)
    if is_crash:
        dest = CRASH_DIR / f"{tag}.rawtiles"
        dest.write_bytes(data)
        crashes.append((tag, f"rc={r.returncode}", out[-1200:]))
        return f"CRASH rc={r.returncode}"
    verdict = "accept" if r.returncode == 0 else "reject"
    line1 = out.splitlines()[0] if out else ""
    res = line1.split("-> ", 1)[1] if "-> " in line1 else line1
    return f"{verdict} ({res})"


def report(tag, desc, outcome):
    results.append((tag, desc, outcome))
    print(f"{tag:34s} {outcome:55s} {desc}")


# ---------------------------------------------------------------- phase 1
def phase1(name: str, src: Path):
    base = src.read_bytes()
    n = len(base)
    print(f"\n=== Phase 1 targeted: {name} ({n} bytes) ===")

    # truncations: mid-magic, mid-header, header boundary, mid-index,
    # mid-blob, just-before-footer, minus 1 byte
    tile_count = struct.unpack_from("<I", base, 88)[0]
    idx_end = 292 + 20 * tile_count
    cuts = sorted(set([0, 3, 100, 291, 292, 296,
                       292 + 10, idx_end - 1, idx_end,
                       (idx_end + n) // 2, n - 5, n - 4, n - 1]))
    for c in cuts:
        if 0 <= c < n:
            report(f"{name}-trunc-{c}", f"truncate to {c} bytes",
                   run_case(f"{name}-trunc-{c}", base[:c]))

    # flip each CRC byte (last 4)
    for i in range(4):
        d = bytearray(base)
        d[n - 4 + i] ^= 0xFF
        report(f"{name}-crcflip-{i}", f"flip footer CRC byte {i}",
               run_case(f"{name}-crcflip-{i}", bytes(d)))

    if tile_count >= 2:
        # swap first two index entries (out of order) and refix CRC? No —
        # deliberately leave CRC stale in one variant, fix it in another so
        # the index-order check itself is exercised past the CRC gate.
        d = bytearray(base)
        e0 = d[292:312]
        e1 = d[312:332]
        d[292:312] = e1
        d[312:332] = e0
        report(f"{name}-idxswap-stalecrc", "swap index entries 0/1, stale CRC",
               run_case(f"{name}-idxswap-stalecrc", bytes(d)))
        report(f"{name}-idxswap-fixcrc", "swap index entries 0/1, CRC refixed",
               run_case(f"{name}-idxswap-fixcrc", refix_crc(bytes(d))))

    # zoom_offsets overlap/leak: header bytes 100..291 are 24 (offset u32,
    # count u32) pairs. Make z entry overlap the next zoom's range.
    d = bytearray(base)
    zmin = d[62]
    off = 100 + 8 * zmin
    cur = struct.unpack_from("<I", d, off + 4)[0]
    struct.pack_into("<I", d, off + 4, cur + 1)  # leak one entry into next z
    report(f"{name}-zoomleak", f"zoom_offsets[{zmin}].count += 1",
           run_case(f"{name}-zoomleak", refix_crc(bytes(d))))

    d = bytearray(base)
    struct.pack_into("<I", d, off, 0xFFFFFFF0)  # absurd offset
    report(f"{name}-zoomoff-wild", f"zoom_offsets[{zmin}].offset = 0xFFFFFFF0",
           run_case(f"{name}-zoomoff-wild", refix_crc(bytes(d))))

    # extensions_offset beyond EOF (header bytes 92..95)
    d = bytearray(base)
    struct.pack_into("<I", d, 92, n + 4096)
    report(f"{name}-extoff-eof", f"extensions_offset = EOF+4096",
           run_case(f"{name}-extoff-eof", refix_crc(bytes(d))))

    d = bytearray(base)
    struct.pack_into("<I", d, 92, 0xFFFFFFFC)
    report(f"{name}-extoff-max", "extensions_offset = 0xFFFFFFFC",
           run_case(f"{name}-extoff-max", refix_crc(bytes(d))))

    if tile_count >= 1:
        # tile length overlapping the footer: entry 0 length (bytes 308..311
        # of entry layout? entry = 20B: z u8, comp u8, reserved u16, x u32,
        # y u32, offset u32, length u32 -> length at entry+16)
        d = bytearray(base)
        off0 = struct.unpack_from("<I", d, 292 + 12)[0]
        struct.pack_into("<I", d, 292 + 16, n - off0)  # runs into CRC footer
        report(f"{name}-len-footer", "entry0 length runs into footer",
               run_case(f"{name}-len-footer", refix_crc(bytes(d))))

        d = bytearray(base)
        struct.pack_into("<I", d, 292 + 16, 0xFFFFFFF0)
        report(f"{name}-len-wild", "entry0 length = 0xFFFFFFF0",
               run_case(f"{name}-len-wild", refix_crc(bytes(d))))

        d = bytearray(base)
        struct.pack_into("<I", d, 292 + 12, 0xFFFFFFF0)
        report(f"{name}-off-wild", "entry0 offset = 0xFFFFFFF0",
               run_case(f"{name}-off-wild", refix_crc(bytes(d))))


def crc32_iso(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def refix_crc(data: bytes) -> bytes:
    """Recompute footer CRC over body so mutations survive the CRC gate."""
    body, _ = data[:-4], data[-4:]
    return body + struct.pack("<I", crc32_iso(body))


# ---------------------------------------------------------------- phase 2
def phase2(iterations: int, seed: int = 20260805):
    rng = random.Random(seed)
    goldens = sorted(GOLDEN_DIR.glob("*.rawtiles"))
    bases = {g.name: g.read_bytes() for g in goldens}
    print(f"\n=== Phase 2 random: {iterations} iters over {len(goldens)} goldens (seed {seed}) ===")
    tallies = {"accept": 0, "reject": 0, "crash": 0}
    for i in range(iterations):
        gname = rng.choice(list(bases))
        d = bytearray(bases[gname])
        nflips = rng.randint(1, 8)
        muts = []
        for _ in range(nflips):
            pos = rng.randrange(len(d))
            bit = 1 << rng.randrange(8)
            d[pos] ^= bit
            muts.append((pos, bit))
        # Half the runs refix the footer CRC so mutations get past the CRC
        # gate and exercise the structural validators; half leave it stale.
        payload = refix_crc(bytes(d)) if rng.random() < 0.5 else bytes(d)
        tag = f"rand-{i:04d}-{gname.removesuffix('.rawtiles')}"
        outcome = run_case(tag, payload)
        if outcome.startswith("CRASH") or outcome == "TIMEOUT":
            tallies["crash"] += 1
            print(f"  CRASH iter {i} on {gname} flips={muts} -> {outcome}")
        elif outcome.startswith("accept"):
            tallies["accept"] += 1
        else:
            tallies["reject"] += 1
        if (i + 1) % 250 == 0:
            print(f"  ...{i+1}/{iterations}  {tallies}")
    print(f"phase2 tallies: {tallies}")


if __name__ == "__main__":
    iters = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    phase1("stanley", STANLEY)
    phase1("pyramid", GOLDEN_DIR / "golden-pyramid.rawtiles")
    phase2(iters)
    print(f"\nTOTAL CRASHES: {len(crashes)}")
    for tag, rc, out in crashes:
        print(f"--- {tag} ({rc}) ---")
        print(out)
