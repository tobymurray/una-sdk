#!/usr/bin/env python3
"""Offline checks of the frame codecs in both probes, against the format recovered in §3c and
the FTS document. No Bluetooth: this exists so a struct or offset mistake is found here rather
than with a watch connected and a limited advertising window."""
import struct
import sys
import importlib.util


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cans = load("cans", "una_cans_probe.py")
v5 = load("v5", "una_v5_probe.py")

failures = []


def check(label, got, want):
    if got != want:
        failures.append(f"{label}: got {got!r}, want {want!r}")
        print(f"  FAIL {label}\n       got  {got!r}\n       want {want!r}")
    else:
        print(f"  ok   {label}")


print("CANS outbound")
check("event Add/Message uid=9000",
      cans.build_event(9000, 0, 1).hex(), "0100" + struct.pack("<I", 9000).hex() + "01")
check("event is 7 bytes", len(cans.build_event(1, 2, 2)), 7)
check("event Modify/Call", cans.build_event(1, 2, 2).hex(), "0102" + "01000000" + "02")

response = cans.build_attribute_response(9000, [(1, b"Hi"), (4, b"Body")])
check("attribute response header", response[:5].hex(), "03" + struct.pack("<I", 9000).hex())
check("first attribute", response[5:10].hex(), "01" + "0200" + b"Hi".hex())
check("second attribute", response[10:].hex(), "04" + "0400" + b"Body".hex())
check("empty attribute list is header only", len(cans.build_attribute_response(1, [])), 5)

print("\nCANS inbound")
request = bytes([3]) + struct.pack("<I", 4242) + bytes([1]) + struct.pack("<H", 32) \
          + bytes([4]) + struct.pack("<H", 512)
decoded = cans.decode_inbound(request)
check("RequestAttributes uid", decoded["uid"], 4242)
check("RequestAttributes count", len(decoded["requested"]), 2)
check("first requested", (decoded["requested"][0]["name"], decoded["requested"][0]["maxLength"]),
      ("Title", 32))
check("second requested", (decoded["requested"][1]["name"], decoded["requested"][1]["maxLength"]),
      ("Message", 512))
check("no trailing bytes reported", "unparsed_trailing_bytes" in decoded, False)

ragged = request + bytes([9, 9])
check("ragged tail is surfaced, not swallowed",
      cans.decode_inbound(ragged).get("unparsed_trailing_bytes"), "0909")

check("ExecutePositiveAction",
      cans.decode_inbound(bytes([4]) + struct.pack("<I", 7))["command"], "ExecutePositiveAction")
check("ExecuteNegativeAction uid",
      cans.decode_inbound(bytes([5]) + struct.pack("<I", 7))["uid"], 7)
check("unknown opcode is flagged",
      cans.decode_inbound(bytes([0x99, 0, 0]))["command"], "unrecognised(0x99)")
check("short RequestAttributes is flagged",
      "note" in cans.decode_inbound(bytes([3, 1])), True)
check("empty frame is flagged", "note" in cans.decode_inbound(b""), True)
check("uid 0 round-trips", cans.decode_inbound(bytes([4]) + struct.pack("<I", 0))["uid"], 0)

print("\nFTS outbound")
check("READ layout", v5.build_read("/a", 0, 4096).hex(),
      "1000" + "0200" + "00000000" + "00100000" + b"/a".hex())
check("READ_PACING layout", v5.build_pacing(4096, 4096).hex(),
      "1201" + "0000" + "00100000" + "00100000")
check("READ_PACING is 12 bytes", len(v5.build_pacing(0, 0)), 12)
check("LISTDIR layout", v5.build_listdir("/Apps/").hex(),
      "5000" + "0600" + b"/Apps/".hex())
check("DIGEST layout", v5.build_digest("/a").hex(), "7000" + "0200" + b"/a".hex())

print("\nFTS inbound")
honest = bytes([0x11, 1, 0, 0]) + struct.pack("<III", 0, 1000, 4) + b"abcd"
parsed = v5.parse_read_data(honest)
check("honest chunk detected", parsed["honest"], True)
check("delivered", parsed["delivered"], 4)
check("total", parsed["total"], 1000)

lying = bytes([0x11, 1, 0, 0]) + struct.pack("<III", 0, 1000, 204) + b"x" * 201
parsed = v5.parse_read_data(lying)
check("una-sdk#272 shape detected as dishonest", parsed["honest"], False)
check("advertised recorded verbatim", parsed["advertised"], 204)
check("delivered recorded verbatim", parsed["delivered"], 201)

check("wrong opcode rejected", v5.parse_read_data(bytes([0x51]) + b"\x00" * 20), None)
check("short header rejected", v5.parse_read_data(bytes([0x11, 1])), None)

entry = (bytes([0x51, 1]) + struct.pack("<H", 5) + struct.pack("<III", 0, 20, 1)
         + struct.pack("<QI", 12345, 999) + b"Alarm")
parsed = v5.parse_list_entry(entry)
check("list entry name", parsed["name"], "Alarm")
check("list entry is_dir", parsed["is_dir"], True)
check("list entry size", parsed["size"], 999)
check("list entry total", parsed["total"], 20)

file_entry = (bytes([0x51, 1]) + struct.pack("<H", 3) + struct.pack("<III", 1, 20, 0)
              + struct.pack("<QI", 1, 42) + b"a.f")
check("file has dir bit clear", v5.parse_list_entry(file_entry)["is_dir"], False)

print(f"\n{'FAILED: ' + str(len(failures)) if failures else 'all checks passed'}")
sys.exit(1 if failures else 0)
