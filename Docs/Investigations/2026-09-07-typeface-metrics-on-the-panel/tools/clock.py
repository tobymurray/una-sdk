import measure as M, sheet as S
from cands import CANDS
PICK=["Poppins","Inter","Inter Tight","Roboto","Roboto Cond","Fira Sans","IBM Plex Sans","Barlow SemiCond","Public Sans","Source Sans 3","Archivo","Montserrat"]
byname={n:(r,b) for n,r,b in CANDS}
print("Clock readout '08:42', SemiBold, cap height 42 px (= Poppins SemiBold 60):\n")
print(f"{'font':17s} {'px':>3s} {'cap':>3s} {'08:42':>6s} {'solid':>5s} {'12-glyph atlas B':>16s}")
rows=[];base=None
for n in PICK:
    p=byname[n][1]; px=S.px_for_cap(p,42)
    if px is None: print(n,"no size"); continue
    r=M.Rast(p,px); adv=r.advance("08:42")
    n_,d,tot=r.atlas_bytes([ord(c) for c in "0123456789:"])
    rows.append((n,px,r.ink_h('H'),adv,M.solid_fraction(r,"0123456789"),tot))
base=next(x for x in rows if x[0]=="Poppins")
for n,px,cap,adv,sol,tot in sorted(rows,key=lambda t:t[3]):
    m="<--" if n=="Poppins" else f"{adv-base[3]:+4d} px"
    print(f"{n:17s} {px:3d} {cap:3d} {adv:6d} {sol:5.2f} {tot:16d}  {m}")
rows2=[(f"{n} {S.px_for_cap(byname[n][1],42)}px", byname[n][1], S.px_for_cap(byname[n][1],42), "08:42") for n in ["Poppins","Inter","Roboto","Roboto Cond","IBM Plex Sans","Barlow SemiCond","Archivo","Montserrat"]]
S.sheet("06_clock.png","SemiBold clock at cap height 42 px (= Poppins SemiBold 60), four panel levels", rows2)
