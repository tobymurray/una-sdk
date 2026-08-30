"""Card E1, minimal: read tiles back out of a .rawtiles and show the panel view.

Two jobs at once.

1. **Verification L4 cannot do.** The validator proves a pack is well-formed, never
   that the pixels are right. Decoding tiles back to an image is the first check
   that the bytes are the bytes we meant.
2. **Cartography preview.** The panel is a Sharp LS012B7DD06A: 240x240, 64 colours
   native, and only three of those 64 codes sit below L* 40. Style iteration
   without seeing that is guesswork.

Layout is re-derived from the spec's tables (offsets cross-checked against
spec-validator-cpp), not by calling slippypack.
"""
import struct, sys
from pathlib import Path
from PIL import Image

PACK = Path(sys.argv[1])
OUT = Path(sys.argv[2])
CENTER_LAT, CENTER_LON = 44.6259, -75.9523
VIEW = 240

OFF_PIXFMT, OFF_TILEDIM, OFF_ZMIN, OFF_ZMAX = 56, 60, 62, 63
OFF_TILECOUNT, OFF_INDEXOFF = 88, 92
INDEX_ENTRY = 20

raw = PACK.read_bytes()
assert raw[:4] == b"RAWT", f"bad magic {raw[:4]!r}"
pixfmt = raw[OFF_PIXFMT]
tile_dim = struct.unpack_from("<H", raw, OFF_TILEDIM)[0]
zmin, zmax = raw[OFF_ZMIN], raw[OFF_ZMAX]
tile_count = struct.unpack_from("<I", raw, OFF_TILECOUNT)[0]
index_off = struct.unpack_from("<I", raw, OFF_INDEXOFF)[0]
print(f"{PACK.name}: pixel_format={pixfmt} tile_dim={tile_dim} zooms {zmin}..{zmax} "
      f"tiles={tile_count} index@{index_off}")
assert pixfmt == 1, f"this preview only decodes ABGR2222 (format 1 per types.rs), got {pixfmt}"

index = {}
for i in range(tile_count):
    b = index_off + i * INDEX_ENTRY
    z, comp = raw[b], raw[b + 1]
    x, y, off, length = struct.unpack_from("<IIII", raw, b + 4)
    assert comp == 0, f"tile {z}/{x}/{y} is compressed; decode not implemented"
    index[(z, x, y)] = (off, length)

expect = tile_dim * tile_dim
sizes = {ln for _, ln in index.values()}
print(f"tile payload sizes present: {sorted(sizes)}  (tile_dim^2 = {expect})")
if sizes != {expect}:
    print("  ** MISMATCH: payload size does not equal tile_dim^2 -- see finding 12")


def decode_abgr2222(buf):
    """One byte per pixel: A B G R, two bits each. Levels are 0/85/170/255."""
    img = Image.new("RGB", (tile_dim, tile_dim))
    px = img.load()
    for i, code in enumerate(buf):
        b = ((code >> 4) & 3) * 85
        g = ((code >> 2) & 3) * 85
        r = (code & 3) * 85
        px[i % tile_dim, i // tile_dim] = (r, g, b)
    return img


# MAP_CARTOGRAPHY_SPEC.md § 3's `preview` column: the sRGB rendering of what the eye
# sees on the panel once adapted. Device RGB (the x85 expansion above) is the
# authoritative byte but oversaturates badly -- judging cartography from it leads you
# to "fix" saturation that is not there. See finding 19.
AS_SEEN = {
    0xFF: (255, 255, 255), 0xEE: (233, 246, 232), 0xDD: (208, 237, 205),
    0xEA: (215, 215, 215), 0xD8: (147, 202, 181), 0xF4: (98, 183, 213),
    0xC5: (165, 148, 122), 0xF0: (0, 132, 194), 0xC3: (210, 79, 98),
    0xC1: (135, 65, 73), 0xD0: (40, 91, 125), 0xC0: (56, 56, 56),
}


def as_seen(rgb):
    """Map a device-RGB triple back to its byte, then to the panel's appearance.

    Off-palette pixels (antialiasing blends between two legal slots) have no spec
    entry, so they are mapped through the nearest slot that does.
    """
    r, g, b = rgb
    code = 0xC0 | ((b // 85) << 4) | ((g // 85) << 2) | (r // 85)
    if code in AS_SEEN:
        return AS_SEEN[code]
    best = min(AS_SEEN, key=lambda c: (((c & 3) * 85 - r) ** 2
                                       + (((c >> 2) & 3) * 85 - g) ** 2
                                       + (((c >> 4) & 3) * 85 - b) ** 2))
    return AS_SEEN[best]


def tile_of(lat, lon, z):
    import math
    n = 1 << z
    lr = math.radians(lat)
    fx = (lon + 180.0) / 360.0 * n
    fy = (1.0 - math.log(math.tan(lr) + 1.0 / math.cos(lr)) / math.pi) / 2.0 * n
    return int(fx), int(fy), fx, fy


for z in range(zmax, zmin - 1, -1):
    tx, ty, fx, fy = tile_of(CENTER_LAT, CENTER_LON, z)
    # Compose a 3x3 neighbourhood so a 240px viewport is always covered.
    canvas = Image.new("RGB", (tile_dim * 3, tile_dim * 3), (255, 0, 255))
    found = 0
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            key = (z, tx + dx, ty + dy)
            if key not in index:
                continue
            off, length = index[key]
            canvas.paste(decode_abgr2222(raw[off:off + length]),
                         ((dx + 1) * tile_dim, (dy + 1) * tile_dim))
            found += 1
    if found == 0:
        continue
    # Centre the viewport on the exact lat/lon within the middle tile.
    cx = int(tile_dim + (fx - tx) * tile_dim)
    cy = int(tile_dim + (fy - ty) * tile_dim)
    view = canvas.crop((cx - VIEW // 2, cy - VIEW // 2, cx + VIEW // 2, cy + VIEW // 2))
    p = OUT.parent / f"{OUT.stem}_z{z}.png"
    view.save(p)
    view.resize((VIEW * 3, VIEW * 3), Image.NEAREST).save(
        OUT.parent / f"{OUT.stem}_z{z}_3x.png")

    # The honest view: what the wearer sees, not what the bytes are.
    seen = Image.new("RGB", view.size)
    vp, sp2 = view.load(), seen.load()
    cache = {}
    for y in range(VIEW):
        for x in range(VIEW):
            v = vp[x, y]
            if v not in cache:
                cache[v] = as_seen(v)
            sp2[x, y] = cache[v]
    seen.save(OUT.parent / f"{OUT.stem}_z{z}_aseen.png")
    seen.resize((VIEW * 3, VIEW * 3), Image.NEAREST).save(
        OUT.parent / f"{OUT.stem}_z{z}_aseen_3x.png")

    codes = {(0xC0 | ((b // 85) << 4) | ((g // 85) << 2) | (r // 85))
             for r, g, b in set(view.getdata())}
    off = sorted(c for c in codes if c not in AS_SEEN)
    print(f"  z{z}: tiles {found}/9 -> {p.name}, slots in viewport: {len(codes)}, "
          f"off-palette: {len(off)}{' ' + ' '.join(hex(c) for c in off) if off else ''}")
