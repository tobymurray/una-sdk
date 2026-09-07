from fontTools.ttLib import TTFont
from cands import CANDS
import os
RANGES = {"ASCII":(0x20,0x7E),"Latin-1 Sup":(0xA0,0xFF),"Latin Ext-A":(0x100,0x17F),
          "Greek":(0x370,0x3FF),"Cyrillic":(0x400,0x4FF)}
print(f"{'font':17s} " + " ".join(f"{k:>12s}" for k in RANGES) + "  hinted?  prime/degree/mu")
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    f = TTFont(reg)
    cmap = set(f.getBestCmap().keys())
    cells=[]
    for k,(a,b) in RANGES.items():
        n = sum(1 for cp in range(a,b+1) if cp in cmap)
        cells.append(f"{n:5d}/{b-a+1:<6d}")
    hint = "yes" if ("fpgm" in f and len(f["fpgm"].program.getBytecode())>0) else "no "
    extras = "".join(c if ord(c) in cmap else "-" for c in "′°µ…–—‘’“”€•♥")
    print(f"{name:17s} " + " ".join(cells) + f"  {hint}      {extras}")
