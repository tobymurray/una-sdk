"""Is the renderer itself nondeterministic, or is slippypack's quantiser?

Fetch every z16 tile covering the Athens bbox twice and compare the PNG bytes.
If any tile's two PNGs differ, the nondeterminism is upstream of slippypack.
"""
import hashlib, math, sys, urllib.request

BASE = "http://localhost:8081/styles/watch"
MINLON, MINLAT, MAXLON, MAXLAT = -76.015, 44.590, -75.889, 44.662
Z = 16
PASSES = int(sys.argv[1]) if len(sys.argv) > 1 else 2


def lon2x(lon, z):
    return int((lon + 180.0) / 360.0 * (1 << z))


def lat2y(lat, z):
    lr = math.radians(lat)
    return int((1.0 - math.log(math.tan(lr) + 1.0 / math.cos(lr)) / math.pi) / 2.0 * (1 << z))


x0, x1 = lon2x(MINLON, Z), lon2x(MAXLON, Z)
y0, y1 = lat2y(MAXLAT, Z), lat2y(MINLAT, Z)  # y grows southward
tiles = [(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
print(f"z{Z}: x {x0}..{x1}, y {y0}..{y1} -> {len(tiles)} tiles, {PASSES} passes", flush=True)


def fetch(x, y):
    with urllib.request.urlopen(f"{BASE}/{Z}/{x}/{y}.png", timeout=60) as r:
        return r.read()


seen = {}
unstable = {}
for p in range(PASSES):
    for (x, y) in tiles:
        raw = fetch(x, y)
        h = hashlib.sha256(raw).hexdigest()
        key = (x, y)
        seen.setdefault(key, set()).add(h)
        if len(seen[key]) > 1:
            unstable.setdefault(key, []).append(raw)
    print(f"pass {p + 1} done; unstable so far: {len(unstable)}", flush=True)

print(f"\ntiles with more than one distinct PNG: {len(unstable)}")
for (x, y), variants in list(unstable.items())[:10]:
    print(f"  {Z}/{x}/{y}: {len(seen[(x, y)])} distinct hashes")
    for i, raw in enumerate(variants):
        open(f"/home/toby/maps/unstable_{Z}_{x}_{y}_v{i}.png", "wb").write(raw)
