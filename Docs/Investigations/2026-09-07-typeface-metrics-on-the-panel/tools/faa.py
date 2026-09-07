"""Prices FAA HFDS 5.3.1-5.3.4, as quoted in the cartography prior-art doc, against real faces.

  - height/width: a 13 px-high capital wants 6-12 px of width, preferred 9
  - stroke width: 1 px for 7-8 px cap, 1-2 px for 9-12, 2 px for 13-14, 2-3 px for 15-20
  - matrix: characters in vertical orientation from at least 9 x 13 pixels
"""
import measure as M, numpy as np
from cands import CANDS
from allsheets import px_for_cap
from fontTools.ttLib import TTFont
import os

CAPS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

def ink_w(r, chars):
    ws = [r.glyph(c)[0].shape[1] for c in chars if r.glyph(c) is not None and r.glyph(c)[0].size]
    return sum(ws)/len(ws)

def stroke(r, ch='I'):
    """Effective stem width in pixels: sum of column levels / 3, through the stem's middle band."""
    p = M.stem_profile(r, ch)
    return None if not p else sum(p)/3.0

print("FAA height/width at a 13 px capital -- 'wants 6-12 px, preferred 9'\n")
print(f"{'face':17s} {'px em':>5s} {'cap':>3s} {'cap ink w':>9s} {'advance':>7s} {'verdict':>22s}")
rows=[]
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    px = px_for_cap(reg, 13); r = M.Rast(reg, px)
    iw = ink_w(r, CAPS); adv = r.measure_adv = sum(r.glyph(c)[3] for c in CAPS)/26
    rows.append((name, px, r.ink_h('H'), iw, adv))
for name,px,cap,iw,adv in sorted(rows,key=lambda t:t[3]):
    v = "preferred (<=9)" if iw<=9.0 else ("within band (<=12)" if iw<=12.0 else "OVER the 12 px band")
    print(f"{name:17s} {px:5d} {cap:3d} {iw:9.1f} {adv:7.1f} {v:>22s}")

print("\n\nFAA stroke width -- 1-2 px at cap 9-12, 2 px at cap 13-14\n")
print(f"{'face':17s} " + "".join(f"{'R'+str(c):>7s}" for c in (9,11,13)) + "   " + "".join(f"{'SB'+str(c):>7s}" for c in (12,14)))
out=[]
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    cells=[]
    for cap in (9,11,13):
        r = M.Rast(reg, px_for_cap(reg, cap)); cells.append(stroke(r))
    for cap in (12,14):
        r = M.Rast(sb, px_for_cap(sb, cap)); cells.append(stroke(r))
    out.append((name, cells))
for name, cells in out:
    print(f"{name:17s} " + "".join("      -" if v is None else f"{v:7.2f}" for v in cells[:3]) + "   " + "".join("      -" if v is None else f"{v:7.2f}" for v in cells[3:]))

print("\n\ncap/em and x/em, measured from the font, against the doc's 0.7 / 0.5 assumption\n")
print(f"{'face':17s} {'cap/em':>7s} {'x/em':>6s} {'cap err vs 0.7':>15s} {'x err vs 0.5':>13s}")
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    tt=TTFont(reg); upm=tt["head"].unitsPerEm; o=tt["OS/2"]
    ce=o.sCapHeight/upm; xe=o.sxHeight/upm
    print(f"{name:17s} {ce:7.3f} {xe:6.3f} {100*(ce/0.7-1):+14.1f}% {100*(xe/0.5-1):+12.1f}%")
