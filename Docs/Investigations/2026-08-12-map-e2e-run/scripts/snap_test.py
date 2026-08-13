"""Would snap-to-declared-slots absorb the renderer's nondeterminism?

Collect distinct renders of the one unstable tile, find the differing pixels,
then compare two quantisation strategies:

  A. nearest of all 64 ABGR2222 slots  (what slippypack does today)
  B. nearest of the palette actually declared by the style (snap-to-slots, C3)

If B maps both variants of a differing pixel to the same slot, snapping absorbs
the jitter. If it maps them apart, snapping only moves the tie boundary.
"""
import hashlib, io, json, urllib.request
from PIL import Image

URL = "http://localhost:8081/styles/watch/16/18941/23671.png"
TRIES = 40

variants = {}
for _ in range(TRIES):
    with urllib.request.urlopen(URL, timeout=60) as r:
        raw = r.read()
    variants.setdefault(hashlib.sha256(raw).hexdigest(), raw)
print(f"{TRIES} fetches -> {len(variants)} distinct PNGs")
if len(variants) < 2:
    raise SystemExit("only one variant seen; rerun")

imgs = [Image.open(io.BytesIO(v)).convert("RGB") for v in list(variants.values())[:2]]
a, b = imgs[0].load(), imgs[1].load()
diffs = [(x, y, a[x, y], b[x, y]) for y in range(256) for x in range(256) if a[x, y] != b[x, y]]
print(f"differing pixels: {len(diffs)}")
for x, y, pa, pb in diffs[:10]:
    print(f"  ({x:3d},{y:3d})  {pa}  vs  {pb}")


def abgr2222_slots():
    """All 64 ABGR2222 colours as RGB, 2 bits per channel expanded to 8."""
    out = []
    for code in range(256):
        b2, g2, r2 = (code >> 4) & 3, (code >> 2) & 3, code & 3
        if (code >> 6) & 3 != 3:      # opaque only
            continue
        out.append((code, (r2 * 85, g2 * 85, b2 * 85)))
    return out


SLOTS64 = abgr2222_slots()


def nearest(rgb, slots):
    return min(slots, key=lambda s: sum((c - d) ** 2 for c, d in zip(rgb, s[1])))[0]


# The declared palette = colours the style actually names, quantised to legal slots.
style = json.load(open("/home/toby/maps/styles/watch/style.json"))
declared = set()
for layer in style["layers"]:
    for key in ("fill-color", "line-color", "background-color", "text-color", "text-halo-color"):
        v = (layer.get("paint") or {}).get(key)
        for tok in json.dumps(v).split('"') if v is not None else []:
            if tok.startswith("#") and len(tok) in (4, 7):
                h = tok[1:]
                if len(h) == 3:
                    h = "".join(c * 2 for c in h)
                declared.add(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)))
declared_slots = sorted({nearest(c, SLOTS64) for c in declared})
SLOTS_DECLARED = [(code, rgb) for code, rgb in SLOTS64 if code in declared_slots]
print(f"\nstyle declares {len(declared)} distinct colours -> {len(SLOTS_DECLARED)} distinct ABGR2222 slots")

print("\npixel                     today (64 slots)     snap (declared)")
absorbed = collapsed = 0
for x, y, pa, pb in diffs:
    t_a, t_b = nearest(pa, SLOTS64), nearest(pb, SLOTS64)
    s_a, s_b = nearest(pa, SLOTS_DECLARED), nearest(pb, SLOTS_DECLARED)
    print(f"  ({x:3d},{y:3d})  0x{t_a:02x} vs 0x{t_b:02x} {'SAME' if t_a == t_b else 'DIFF'}"
          f"      0x{s_a:02x} vs 0x{s_b:02x} {'SAME' if s_a == s_b else 'DIFF'}")
    if t_a == t_b:
        absorbed += 1
    if s_a == s_b:
        collapsed += 1
print(f"\nabsorbed by today's quantiser: {absorbed}/{len(diffs)}")
print(f"absorbed by snap-to-declared : {collapsed}/{len(diffs)}")
