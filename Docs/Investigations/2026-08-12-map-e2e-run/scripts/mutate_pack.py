"""Mutate a valid pack and confirm the independent validator rejects it.

A validator that prints OK for everything is decorative. Each mutation below
should fail, except M2 which is expected to PASS -- and that expectation is the
point: it shows what "validated" does and does not mean.
"""
import shutil, subprocess, sys, zlib
from pathlib import Path

VALIDATOR = sys.argv[1]
SRC = Path(sys.argv[2])
WORK = Path("/tmp/claude-1000/-home-toby-git-cpp-una-sdk/245509cc-320e-4252-b4c3-485e68e3eb1c/scratchpad/mut")
WORK.mkdir(exist_ok=True)

orig = SRC.read_bytes()
PAYLOAD_BYTE = len(orig) // 2          # somewhere in the middle: tile data


def fix_crc(buf: bytearray) -> bytearray:
    crc = zlib.crc32(bytes(buf[:-4])) & 0xFFFFFFFF
    buf[-4:] = crc.to_bytes(4, "little")
    return buf


def run(name, buf, expect_fail=True):
    p = WORK / f"{name}.rawtiles"
    p.write_bytes(bytes(buf))
    r = subprocess.run([VALIDATOR, str(p)], capture_output=True, text=True)
    failed = r.returncode != 0
    verdict = "REJECTED" if failed else "accepted"
    ok = failed == expect_fail
    detail = (r.stderr or r.stdout).strip().splitlines()
    msg = detail[0] if detail else ""
    print(f"  {'PASS' if ok else 'PROBLEM'}  {name:<22} {verdict:<9} {msg[:78]}")
    return ok


print(f"validating mutations of {SRC.name} ({len(orig)} bytes)\n")
results = []

b = bytearray(orig); b[PAYLOAD_BYTE] ^= 0xFF
results.append(run("M1-payload-bitflip", b))

b = bytearray(orig); b[PAYLOAD_BYTE] ^= 0xFF; fix_crc(b)
results.append(run("M2-payload-crc-fixed", b, expect_fail=False))

b = bytearray(orig); b[0] ^= 0xFF; fix_crc(b)
results.append(run("M3-magic-corrupt", b))

b = bytearray(orig[:-1])
results.append(run("M4-truncated-1byte", b))

b = bytearray(orig[: len(orig) // 2])
results.append(run("M5-truncated-half", b))

# tile_dim_px lives in the fixed header; corrupt it and repair the CRC so the
# only remaining defect is structural.
for off in range(8, 64):
    b = bytearray(orig)
    if int.from_bytes(b[off:off + 2], "little") == 256:
        b[off:off + 2] = (255).to_bytes(2, "little")
        fix_crc(b)
        results.append(run(f"M6-tile_dim@{off}", b))
        break

b = bytearray(orig); b[-4:] = b"\x00\x00\x00\x00"
results.append(run("M7-crc-zeroed", b))

print(f"\n{sum(results)}/{len(results)} mutations behaved as expected")
sys.exit(0 if all(results) else 1)
