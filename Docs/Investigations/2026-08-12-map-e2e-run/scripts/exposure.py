"""How exposed is each quantiser to a +/-1 renderer jitter?

The renderer's error is about 1/255 in a channel. The pack's observed error was a
whole ABGR2222 level -- 85/255. So the quantiser is an amplifier: a sub-perceptual
jitter becomes a full palette step whenever the pre-quantisation value sits next to
a decision boundary.

"Exposed" here means exact, not statistical: perturb the pixel by +/-1 on each
channel in turn and check whether the chosen slot changes. Measured for
  A. nearest of all 64 ABGR2222 slots (today)
  B. nearest of the slots the style declares (snap-to-slots, C3)
"""
import io, json, urllib.request
from PIL import Image

TILES = ["16/18941/23671", "16/18941/23673", "15/9470/11835", "14/4735/5917", "12/1183/1479"]


def slots64():
    out = []
    for code in range(256):
        if (code >> 6) & 3 != 3:
            continue
        b2, g2, r2 = (code >> 4) & 3, (code >> 2) & 3, code & 3
        out.append((code, (r2 * 85, g2 * 85, b2 * 85)))
    return out


S64 = slots64()


def nearest(rgb, slots):
    best, bestd = None, None
    for code, srgb in slots:
        d = sum((c - s) ** 2 for c, s in zip(rgb, srgb))
        if bestd is None or d < bestd:
            best, bestd = code, d
    return best


style = json.load(open("/home/toby/maps/styles/watch/style.json"))
declared = set()
for layer in style["layers"]:
    paint = layer.get("paint") or {}
    for key in ("fill-color", "line-color", "background-color", "text-color", "text-halo-color"):
        if key not in paint:
            continue
        for tok in json.dumps(paint[key]).split('"'):
            if tok.startswith("#") and len(tok) in (4, 7):
                h = tok[1:]
                if len(h) == 3:
                    h = "".join(c * 2 for c in h)
                declared.add(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)))
SDEC = [(c, r) for c, r in S64 if c in {nearest(d, S64) for d in declared}]
print(f"style declares {len(declared)} colours -> {len(SDEC)} ABGR2222 slots: "
      f"{[hex(c) for c, _ in SDEC]}\n")


def exposed(rgb, slots):
    base = nearest(rgb, slots)
    for ch in range(3):
        for delta in (-1, 1):
            p = list(rgb)
            p[ch] = max(0, min(255, p[ch] + delta))
            if nearest(tuple(p), slots) != base:
                return True
    return False


print(f"{'tile':<18}{'pixels':>8}{'exposed@64':>12}{'exposed@snap':>14}")
tot64 = totsnap = totpx = 0
for t in TILES:
    with urllib.request.urlopen(f"http://localhost:8081/styles/watch/{t}.png", timeout=60) as r:
        img = Image.open(io.BytesIO(r.read())).convert("RGB")
    px = img.load()
    w, h = img.size
    e64 = esnap = 0
    cache = {}
    for y in range(h):
        for x in range(w):
            rgb = px[x, y]
            if rgb not in cache:
                cache[rgb] = (exposed(rgb, S64), exposed(rgb, SDEC))
            a, b = cache[rgb]
            e64 += a
            esnap += b
    n = w * h
    tot64 += e64; totsnap += esnap; totpx += n
    print(f"{t:<18}{n:>8}{e64:>12}{esnap:>14}")
print(f"\n{'TOTAL':<18}{totpx:>8}{tot64:>12}{totsnap:>14}")
print(f"exposure rate today: {tot64 / totpx * 100:.4f} %")
print(f"exposure rate snap : {totsnap / totpx * 100:.4f} %")
