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
    distinct = len(set(view.getdata()))
    print(f"  z{z}: tiles {found}/9 -> {p.name}, distinct colours in viewport: {distinct}")
