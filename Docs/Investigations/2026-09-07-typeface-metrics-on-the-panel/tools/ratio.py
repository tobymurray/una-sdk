from fontTools.ttLib import TTFont
from cands import CANDS
import os
print("From the OS/2 table: how much of the em a face spends below the cap line\n")
print(f"{'font':17s} {'upm':>5s} {'cap':>5s} {'x-ht':>5s} {'x/cap':>6s} {'cap/em':>7s}  em needed for cap 12 px")
rows=[]
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    f=TTFont(reg); o=f["OS/2"]; upm=f["head"].unitsPerEm
    cap=getattr(o,"sCapHeight",None); xh=getattr(o,"sxHeight",None)
    if not cap: continue
    rows.append((name,upm,cap,xh,xh/cap,cap/upm,12*upm/cap))
for name,upm,cap,xh,xc,ce,need in sorted(rows,key=lambda r:-r[5]):
    print(f"{name:17s} {upm:5d} {cap:5d} {xh:5d} {xc:6.2f} {ce:7.3f}  {need:5.1f} px")
