"""Enumerate the (layer, kind) pairs actually present in the Athens extract.

Minimal MVT reader -- MVT is protobuf, and for this we only need layer names, the
per-layer key/value string tables, and each feature's tag pairs. No dependency.

Wire format (vector_tile.proto):
  Tile.layers        = field 3, length-delimited
  Layer.name         = field 1 (string)
  Layer.features     = field 2 (message)
  Layer.keys         = field 3 (string, repeated)
  Layer.values       = field 4 (message, repeated)
  Layer.extent       = field 5 (varint)
  Feature.tags       = field 2 (packed varint, key/value index pairs)
  Feature.type       = field 3 (varint: 1 point, 2 line, 3 polygon)
  Value.string_value = field 1 (string)
"""
import gzip, sys, urllib.request
from collections import defaultdict


def varint(buf, i):
    shift = result = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, i
        shift += 7


def fields(buf, start=0, end=None):
    """Yield (field_number, wire_type, payload_or_value, next_index)."""
    i, end = start, len(buf) if end is None else end
    while i < end:
        key, i = varint(buf, i)
        fnum, wtype = key >> 3, key & 7
        if wtype == 0:
            val, i = varint(buf, i)
            yield fnum, wtype, val
        elif wtype == 2:
            ln, i = varint(buf, i)
            yield fnum, wtype, buf[i:i + ln]
            i += ln
        elif wtype == 5:
            yield fnum, wtype, buf[i:i + 4]
            i += 4
        elif wtype == 1:
            yield fnum, wtype, buf[i:i + 8]
            i += 8
        else:
            raise ValueError(f"wire type {wtype}")


GEOM = {1: "point", 2: "line", 3: "polygon"}


def read_tile(raw):
    out = defaultdict(lambda: defaultdict(set))
    for fnum, _, payload in fields(raw):
        if fnum != 3:
            continue
        name, keys, values, feats = None, [], [], []
        for f2, _, p2 in fields(payload):
            if f2 == 1:
                name = p2.decode()
            elif f2 == 2:
                feats.append(p2)
            elif f2 == 3:
                keys.append(p2.decode())
            elif f2 == 4:
                sval = None
                for f3, _, p3 in fields(p2):
                    if f3 == 1:
                        sval = p3.decode()
                values.append(sval)
        for feat in feats:
            tags, gtype = [], None
            for f3, wt, p3 in fields(feat):
                if f3 == 2 and wt == 2:
                    j = 0
                    while j < len(p3):
                        v, j = varint(p3, j)
                        tags.append(v)
                elif f3 == 3 and wt == 0:
                    gtype = p3
            attrs = {}
            for k, v in zip(tags[0::2], tags[1::2]):
                if k < len(keys) and v < len(values):
                    attrs[keys[k]] = values[v]
            kind = attrs.get("kind", "(no kind)")
            detail = attrs.get("kind_detail")
            label = kind + (f":{detail}" if detail else "")
            out[name][GEOM.get(gtype, "?")].add(label)
    return out


TILES = ["15/9470/11835", "15/9471/11835", "14/4735/5917", "13/2367/2958", "12/1183/1479"]
merged = defaultdict(lambda: defaultdict(set))
for t in TILES:
    with urllib.request.urlopen(
            f"http://localhost:8081/data/pmtiles/{t}.pbf", timeout=60) as r:
        raw = r.read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    for layer, byg in read_tile(raw).items():
        for g, kinds in byg.items():
            merged[layer][g] |= kinds

for layer in sorted(merged):
    print(f"\n{layer}")
    for g in sorted(merged[layer]):
        ks = sorted(merged[layer][g])
        print(f"  {g:<8} {', '.join(ks)}")
