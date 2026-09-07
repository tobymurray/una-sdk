import measure as M, numpy as np, os, math
from cands import CANDS
ALNUM="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
FOOTER="R1 TOGGLE  R2 BACK"


def px_for_xheight(path, target):
    """Smallest px whose rendered 'x' ink height equals target."""
    best=None
    for px in range(8, 40):
        h = M.Rast(path, px).ink_h('x')
        if h == target:
            return px
        if h > target and best is None:
            best = px  # overshoot
    return best

def row(name, path, target_x, weight):
    px = px_for_xheight(path, target_x)
    if px is None: return None
    r = M.Rast(path, px)
    n, data, total = r.atlas_bytes()
    asc=math.ceil(r.face.size.ascender/64); desc=math.ceil(-r.face.size.descender/64)
    adv = r.advance(FOOTER)
    xo, xt, closed = M.counter_report(r)
    return dict(name=name, weight=weight, px=px, x=r.ink_h('x'), cap=r.ink_h('H'),
                line=asc+desc, solid=M.solid_fraction(r, ALNUM), footer=adv,
                bytes=total, stem=M.stem_profile(r,'l'), counters=f"{xo}/{xt}", closed=closed)

for target_x, label, weight_idx in [(10, "Regular, x-height 10 px  (= Poppins Regular 18, Barcode body)", 1),
                                    (9,  "Regular, x-height 9 px   (= Poppins Regular 16, Spin labels)", 1),
                                    (7,  "Regular, x-height 7 px   (= Poppins Regular 12, NotifyToggle hint)", 1),
                                    (10, "SemiBold, x-height 10 px (= Poppins SemiBold 18, NotifyToggle words)", 2)]:
    print("="*118)
    print(label)
    print("="*118)
    print(f"{'font':17s} {'px':>3s} {'x':>2s} {'cap':>3s} {'line':>4s} {'solid':>5s} {'footer':>6s} {'ASCII B':>7s}  {'stem of l':16s} {'counters':>8s} {'filled'}")
    rows=[]
    for name, reg, sb in CANDS:
        p = reg if weight_idx==1 else sb
        if not os.path.exists(p): print("missing", p); continue
        rr = row(name, p, target_x, weight_idx)
        if rr: rows.append(rr)
    base = next(r for r in rows if r['name']=="Poppins")
    for r in sorted(rows, key=lambda r: r['footer']):
        d = r['footer'] - base['footer']
        mark = " <-- baseline" if r['name']=="Poppins" else f" {d:+4d} px"
        print(f"{r['name']:17s} {r['px']:3d} {r['x']:2d} {r['cap']:3d} {r['line']:4d} {r['solid']:5.2f} {r['footer']:6d} {r['bytes']:7d}  {str(r['stem']):16s} {r['counters']:>8s} {r['closed'] or '-':6s}{mark}")
    print()
