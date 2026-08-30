#!/usr/bin/env python3
"""M5 -- write throughput at realistic sizes. Writes to an allowlisted scratch path, times it,
reads it back and byte-compares, logs one JSON record per attempt to raw/m5_write_sizes.jsonl.

Usage: python3 bench_m5.py <device-address> <size-bytes> [path-suffix]
"""
import asyncio
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "2026-07-29-hardware-config-recovery", "prototype"))
from una_ble_client import find_fts_characteristic, write_file, read_file  # noqa: E402
from dbus_fast import BusType  # noqa: E402
from dbus_fast.aio import MessageBus  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RAW_LOG = os.path.join(HERE, "raw", "m5_write_sizes.jsonl")


def make_payload(size: int) -> bytes:
    # Deterministic, cheap to generate, but not all-zero/all-same-byte so a truncation or
    # corruption bug can't hide behind a payload that already looks "flat".
    unit = hashlib.sha256(b"una-ble-write-bench").digest()  # 32 bytes
    reps = size // len(unit) + 1
    return (unit * reps)[:size]


async def main():
    address, size = sys.argv[1], int(sys.argv[2])
    suffix = sys.argv[3] if len(sys.argv) > 3 else str(size)
    verify = "--no-verify" not in sys.argv
    filepath = f"/Apps/HelloWorld/ble_bench_m5_{suffix}.bin"
    payload = make_payload(size)

    bus = await MessageBus(bus_type=BusType.SYSTEM, negotiate_unix_fd=True).connect()
    char_path = await find_fts_characteristic(bus, address)

    os.makedirs(os.path.dirname(RAW_LOG), exist_ok=True)
    print(f"writing {size} bytes to {filepath} ...")
    t0 = time.monotonic()
    rec = {"measurement": "M5", "size": size, "path": filepath}
    last_report = [0.0]

    def progress(sent, total):
        now = time.monotonic()
        if now - last_report[0] >= 15.0 or sent >= total:
            rate = sent / (now - t0) if now > t0 else 0
            print(f"  progress: {sent}/{total} ({100*sent/total:.1f}%) "
                  f"{now-t0:.1f}s elapsed, {rate:.1f} B/s so far")
            last_report[0] = now

    try:
        final_offset = await write_file(bus, char_path, filepath, payload, progress=progress)
        elapsed = time.monotonic() - t0
        rec.update(write_elapsed_s=elapsed, write_bytes_per_sec=size / elapsed,
                   final_offset=final_offset, write_ok=(final_offset == size))
        print(f"write done in {elapsed:.3f}s = {size/elapsed:.1f} B/s (final_offset={final_offset})")
    except Exception as exc:  # noqa: BLE001 -- record the failure, don't lose the data point
        elapsed = time.monotonic() - t0
        rec.update(write_elapsed_s=elapsed, write_ok=False, write_error=str(exc))
        print(f"WRITE FAILED after {elapsed:.3f}s: {exc}")
        with open(RAW_LOG, "a") as f:
            f.write(json.dumps(rec) + "\n")
        sys.exit(1)

    if verify:
        print("reading back for verification ...")
        t1 = time.monotonic()
        readback = await read_file(bus, char_path, filepath, chunk_len=201)
        read_elapsed = time.monotonic() - t1
        rec["read_elapsed_s"] = read_elapsed
        if readback is None:
            rec["verify_ok"] = False
            print("READBACK FAILED")
        else:
            rec["verify_ok"] = (readback == payload)
            rec["readback_len"] = len(readback)
            print(f"readback {len(readback)} bytes in {read_elapsed:.3f}s -- "
                  f"{'MATCH' if rec['verify_ok'] else 'MISMATCH'}")
    else:
        rec["verify_ok"] = None
        print("skipping full readback verification (--no-verify); run verify_write.py separately")

    with open(RAW_LOG, "a") as f:
        f.write(json.dumps(rec) + "\n")


if __name__ == "__main__":
    asyncio.run(main())
