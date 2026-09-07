import measure as M, numpy as np
from PIL import Image, ImageDraw, ImageFont
from cands import CANDS

LEVELMAP = np.array([0, 85, 170, 255], np.uint8)   # the panel's four greys
UI = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 22)
UIS = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 17)
TITLE = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 26)

def px_for_cap(path, target):
    """px whose rendered cap height is closest to target; ties go to the larger."""
    best = None
    for px in range(8, 130):
        h = M.Rast(path, px).ink_h('H')
        if best is None or abs(h - target) <= abs(best[1] - target):
            best = (px, h)
        if h > target + 2:
            break
    return best[0]

def build(out, title, subtitle, string, cap, weight, zoom, labelw=290):
    rows = []
    for name, reg, sb in CANDS:
        p = sb if weight else reg
        px = px_for_cap(p, cap)
        r = M.Rast(p, px)
        arr, _ = M.render_string(r, string)
        rows.append(dict(name=name, px=px, adv=r.advance(string), cap=r.ink_h('H'), arr=arr))
    base = next(x for x in rows if x['name'] == "Poppins")
    rest = sorted((x for x in rows if x['name'] != "Poppins"), key=lambda x: x['adv']/x['cap'])
    rows = [base] + rest

    gap = 10
    W = max(x['arr'].shape[1] for x in rows)
    body_h = sum(x['arr'].shape[0] + gap for x in rows)
    top = 92
    canvas = np.zeros((body_h, W), np.uint8)
    y = 0
    for x in rows:
        a = x['arr']
        canvas[y:y+a.shape[0], :a.shape[1]] = LEVELMAP[a]
        x['y'] = y
        y += a.shape[0] + gap
    big = Image.fromarray(canvas).resize((W*zoom, body_h*zoom), Image.NEAREST).convert("RGB")

    full = Image.new("RGB", (labelw + W*zoom + 215, body_h*zoom + top + 24), (8, 8, 10))
    full.paste(big, (labelw, top))
    d = ImageDraw.Draw(full)
    d.text((22, 22), title, fill=(150, 210, 255), font=TITLE)
    d.text((22, 56), subtitle, fill=(120, 120, 130), font=UIS)
    for x in rows:
        yy = top + x['y']*zoom
        h = x['arr'].shape[0]*zoom
        col = (255, 215, 120) if x['name'] == "Poppins" else (215, 215, 225)
        d.text((22, yy + h//2 - 22), x['name'], fill=col, font=UI)
        d.text((22, yy + h//2 + 4), f"{x['px']} px em, cap {x['cap']}", fill=(115, 115, 125), font=UIS)
        norm = x['adv'] * cap / x['cap']
        bnorm = base['adv'] * cap / base['cap']
        dx = norm - bnorm
        note = "baseline" if x['name'] == "Poppins" else f"{dx:+.0f} px at cap {cap}"
        nc = (255, 215, 120) if x['name'] == "Poppins" else ((130, 220, 150) if dx < -0.5 else (235, 130, 130))
        d.text((labelw + W*zoom + 14, yy + h//2 - 22), f"{x['adv']} px", fill=(215, 215, 225), font=UI)
        d.text((labelw + W*zoom + 14, yy + h//2 + 4), note, fill=nc, font=UIS)
    full.save(out)
    print("wrote", out, full.size)
    return rows

build_ = build
build("all_08_zoom_confusables.png",
      "'oa  Il1  ce' at cap height 9 px, magnified 12x",
      "TextKit's own pipeline: FreeType 2.13.2 light autohint, coverage rounded at 43/128/213, the panel's four greys, bright on dark.",
      "oa Il1 ce", 9, 0, 12)
build("all_06_clock.png",
      "'08:42' at cap height 42 px  (= Poppins SemiBold 60, Spin's clock)",
      "SemiBold, same pipeline. Width shown is the advance every face needs for the same cap height on a 240 px round face.",
      "08:42", 42, 1, 4)
