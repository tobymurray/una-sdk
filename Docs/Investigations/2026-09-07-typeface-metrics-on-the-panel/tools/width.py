from fontTools.ttLib import TTFont
from cands import CANDS
import os
print("Advance width per unit of cap height, from the font's own units (design width, no rasterizer)\n")
print(f"{'font':17s} {'A-Z avg':>8s} {'0-9 avg':>8s} {'vs Poppins':>11s}")
rows=[]
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    f=TTFont(reg); cap=f["OS/2"].sCapHeight
    cm=f.getBestCmap(); hm=f["hmtx"]
    def avg(chars):
        vs=[hm[cm[ord(c)]][0] for c in chars if ord(c) in cm]
        return sum(vs)/len(vs)/cap
    rows.append((name, avg("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), avg("0123456789")))
base=next(r for r in rows if r[0]=="Poppins")
for name,az,dg in sorted(rows,key=lambda r:r[1]):
    print(f"{name:17s} {az:8.3f} {dg:8.3f} {100*(az/base[1]-1):+10.1f}%")
