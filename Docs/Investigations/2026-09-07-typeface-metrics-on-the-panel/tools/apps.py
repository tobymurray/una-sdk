import measure as M, sheet as S
from cands import CANDS
byname={n:(r,b) for n,r,b in CANDS}
PICK=["Poppins","Inter Tight","Roboto Cond","Source Sans 3","IBM Plex Sans","Inter","Roboto"]
LATIN = sorted(set(list(range(0x20,0x7F)) + list(range(0xA0,0x180))
                 + [0x2026,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x20AC,0x2022]))
ASCII = list(range(0x20,0x7F))

CASES = [
 ("NotifyToggle footer", "R1 TOGGLE  R2 BACK", 0, 9,  "chord 129 at y=220; 122 at y=222"),
 ("NotifyToggle words",  "NOTIFICATIONS",      1, 12, "centre row, chord 240"),
 ("Barcode id tier",     "0123456789ABCD",     1, 14, "demoted to small face above 186"),
 ("Barcode caption",     "WWWWWWWWWWWW",       0, 13, "160 px box; overflows today"),
 ("Spin clock",          "08:42",              1, 42, "240 px face"),
 ("Spin YES / NO",       "YES  NO",            1, 17, ""),
]
print("Advance in px at the same cap height Poppins draws today\n")
w = 15
print(f"{'case':22s} " + "".join(f"{n[:13]:>15s}" for n in PICK))
for label, s, wi, cap, note in CASES:
    cells=[]
    for n in PICK:
        p = byname[n][wi]; px = S.px_for_cap(p, cap)
        cells.append(M.Rast(p,px).advance(s) if px else None)
    base=cells[0]
    out=f"{label:22s} "
    for i,(n,v) in enumerate(zip(PICK,cells)):
        out += f"{v:>9d}{'      ' if i==0 else f'{v-base:+6d}'}" if v else f"{'-':>15s}"
    print(out + ("   " + note if note else ""))

print("\n\nAtlas cost of each app's faces, same cap heights (bytes: glyph data + 12/node)\n")
APPS = [
 ("NotifyToggle", [(1,12,ASCII),(0,9,ASCII)]),
 ("Barcode",      [(1,14,ASCII),(0,13,LATIN)]),
 ("Spin",         [(0,11,LATIN),(1,12,ASCII),(1,17,[ord(c) for c in " YESNO"]),
                   (1,22,[ord(c) for c in " SPIN"]),(1,19,[ord(c) for c in "0123456789:"]),
                   (1,25,[ord(c) for c in "0123456789:"]),(1,34,[ord(c) for c in "0123456789:"]),
                   (1,42,[ord(c) for c in "0123456789:"])]),
]
print(f"{'app':14s} " + "".join(f"{n[:13]:>15s}" for n in PICK))
for app, faces in APPS:
    cells=[]
    for n in PICK:
        tot=0; ok=True
        for wi, cap, cps in faces:
            p = byname[n][wi]; px = S.px_for_cap(p, cap)
            if px is None: ok=False; break
            tot += M.Rast(p,px).atlas_bytes(cps)[2]
        cells.append(tot if ok else None)
    base=cells[0]
    out=f"{app:14s} "
    for i,v in enumerate(cells):
        out += f"{v:>9d}{'      ' if i==0 else f'{v-base:+6d}'}" if v else f"{'-':>15s}"
    print(out)
