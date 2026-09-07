import measure as M, os, math, numpy as np
from cands import CANDS
FOOTER="R1 TOGGLE  R2 BACK"
CAPS="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

def px_for_cap(path, target):
    for px in range(8, 46):
        if M.Rast(path, px).ink_h('H') == target: return px
    return None

def dist(a, b):
    """Normalised disagreement between two quantised glyph bitmaps, aligned at baseline+left."""
    if a is None or b is None: return None
    (la, ta, xa, _), (lb, tb, xb, _) = a, b
    if la.size==0 or lb.size==0: return None
    top = max(ta, tb); left = min(xa, xb)
    H = top + max(la.shape[0]-ta, lb.shape[0]-tb)
    W = max(xa+la.shape[1], xb+lb.shape[1]) - left
    A = np.zeros((H,W)); B = np.zeros((H,W))
    A[top-ta:top-ta+la.shape[0], xa-left:xa-left+la.shape[1]] = la
    B[top-tb:top-tb+lb.shape[0], xb-left:xb-left+lb.shape[1]] = lb
    return float(np.abs(A-B).sum() / max(A.sum(), B.sum()))

PAIRS = [("O","0"),("I","l"),("l","1"),("I","1"),("a","o"),("S","5"),("B","8"),("G","6"),("c","e"),("u","v")]

for target_cap, label, wi in [(12, "SemiBold, cap 12 px  (Poppins SemiBold 18 = NotifyToggle words)", 2),
                              (14, "SemiBold, cap 14 px  (Poppins SemiBold 20 = Barcode id tier)", 2),
                              (9,  "Regular, cap 9 px    (Poppins Regular 12 = NotifyToggle footer)", 1),
                              (11, "Regular, cap 11 px   (Poppins Regular 16 = Spin labels)", 1)]:
    print("="*112); print(label); print("="*112)
    hdr = f"{'font':17s} {'px':>3s} {'x':>2s} {'solid':>5s} {'footer':>6s} {'B':>5s} " + " ".join(f"{a}/{b}" for a,b in PAIRS)
    print(hdr)
    rows=[]
    for name, reg, sb in CANDS:
        p = reg if wi==1 else sb
        px = px_for_cap(p, target_cap)
        if px is None: continue
        r = M.Rast(p, px)
        n,d,tot = r.atlas_bytes()
        ds = [dist(r.glyph(a), r.glyph(b)) for a,b in PAIRS]
        rows.append((name, px, r.ink_h('x'), M.solid_fraction(r,CAPS), r.advance(FOOTER), tot, ds))
    base = next(x for x in rows if x[0]=="Poppins")
    for name,px,x,solid,adv,tot,ds in sorted(rows, key=lambda t:t[4]):
        ds_s = " ".join("  - " if v is None else f"{v:.2f}" for v in ds)
        mark = "<--" if name=="Poppins" else f"{adv-base[4]:+4d}"
        print(f"{name:17s} {px:3d} {x:2d} {solid:5.2f} {adv:6d} {tot:5d} {ds_s} {mark}")
    print()
