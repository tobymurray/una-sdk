import measure as M, numpy as np, os, sys
from PIL import Image, ImageDraw, ImageFont
from cands import CANDS
LEVELMAP = np.array([0, 85, 170, 255], dtype=np.uint8)   # the panel's four greys
SCALE = 4
LBL = ImageFont.load_default()

def px_for_cap(path, target):
    best=None
    for px in range(8, 110):
        h=M.Rast(path, px).ink_h('H')
        if h == target: return px
        if h > target: return px-1 if px>8 else px
    return None
def px_for_x(path, target):
    for px in range(8, 110):
        if M.Rast(path, px).ink_h('x') == target: return px
    return None

def strip(path, px, s):
    r = M.Rast(path, px)
    arr, base = M.render_string(r, s)
    return arr

def sheet(out, title, rows, gap=6, labelw=150):
    """rows = [(label, path, px, string)]"""
    imgs = [(lab, strip(p, px, s)) for lab, p, px, s in rows]
    W = max(a.shape[1] for _, a in imgs)
    H = sum(a.shape[0] + gap for _, a in imgs) + 26
    canvas = np.zeros((H, W), np.uint8)
    y = 20
    ys = []
    for lab, a in imgs:
        canvas[y:y+a.shape[0], :a.shape[1]] = LEVELMAP[a]
        ys.append(y); y += a.shape[0] + gap
    big = Image.fromarray(canvas).resize((W*SCALE, H*SCALE), Image.NEAREST).convert("RGB")
    full = Image.new("RGB", (labelw + W*SCALE, H*SCALE), (0,0,0))
    full.paste(big, (labelw, 0))
    d = ImageDraw.Draw(full)
    d.text((6, 4), title, fill=(120,200,255), font=LBL)
    for (lab, a), yy in zip(imgs, ys):
        d.text((6, yy*SCALE + 4), lab, fill=(200,180,110), font=LBL)
        d.text((labelw + a.shape[1]*SCALE + 6, yy*SCALE + 4), f"{a.shape[1]}px", fill=(90,90,90), font=LBL)
    full.save(out)
    print("wrote", out, full.size)
