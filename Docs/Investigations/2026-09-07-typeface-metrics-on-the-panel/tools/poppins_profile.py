import measure as M
from cands import POPPINS as F
ALNUM="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
print("Poppins as the apps draw it (TextKit pipeline, FreeType 2.13.2 light + 43/128/213)\n")
print(f"{'face':22s} {'x-ht':>5s} {'cap':>4s} {'asc/desc':>9s} {'solid':>6s} {'counters open':>14s} {'closed'}")
for font,px,who in [("Poppins-Regular.ttf",12,"NotifyToggle hint"),("Poppins-Regular.ttf",14,"—"),
                    ("Poppins-Regular.ttf",16,"Spin labels"),("Poppins-Regular.ttf",18,"Barcode body"),
                    ("Poppins-SemiBold.ttf",18,"NotifyToggle words"),("Poppins-SemiBold.ttf",20,"Barcode id"),
                    ("Poppins-SemiBold.ttf",24,"Spin YES/NO")]:
    r=M.Rast(F+font,px)
    import math
    asc=math.ceil(r.face.size.ascender/64); desc=math.ceil(-r.face.size.descender/64)
    xo,xt,cl = M.counter_report(r)
    print(f"{font.replace('Poppins-','').replace('.ttf',''):10s} {px:2d}px      {r.ink_h('x'):3d}  {r.ink_h('H'):3d}   {asc:3d}/{desc:<3d}   {M.solid_fraction(r,ALNUM):.2f}   {xo:2d}/{xt:<2d}         {cl or '-':10s} {who}")
print()
print("Stem column levels through the middle of 'l' (0=off,3=full):")
for font,px in [("Poppins-Regular.ttf",12),("Poppins-Regular.ttf",14),("Poppins-Regular.ttf",16),("Poppins-Regular.ttf",18),("Poppins-SemiBold.ttf",18),("Poppins-SemiBold.ttf",20)]:
    r=M.Rast(F+font,px)
    print(f"  {font.replace('Poppins-','').replace('.ttf',''):9s} {px:2d}px  {M.stem_profile(r,'l')}")
print()
print("Real strings, advance in px, against the chord they must fit:")
for s,font,px,note in [("R1 TOGGLE  R2 BACK","Poppins-Regular.ttf",12,"footer y=220, chord 129"),
                       ("R1 TOGGLE  R2 BACK","Poppins-Regular.ttf",14,"same row"),
                       ("NOTIFICATIONS","Poppins-SemiBold.ttf",18,""),
                       ("0123456789ABCD","Poppins-SemiBold.ttf",20,"Barcode id tier, 186/187 threshold"),
                       ("WWWWWWWWWWWW","Poppins-Regular.ttf",18,"caption box 160")]:
    r=M.Rast(F+font,px)
    print(f"  {s!r:22s} {font.replace('Poppins-','').replace('.ttf',''):9s} {px:2d}px = {r.advance(s):3d} px   {note}")
