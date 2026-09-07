"""The two tables the README quotes that no other tool prints verbatim."""
import measure as M, numpy as np
from cands import CANDS, POPPINS
from allsheets import px_for_cap
ALNUM="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
print("Section 8 -- partial-ink fraction in Poppins, by rendered cap height\n")
for lbl, f in (("Regular","Poppins-Regular.ttf"),("SemiBold","Poppins-SemiBold.ttf")):
    for cap in (9,11,12,13,14):
        px = px_for_cap(POPPINS+f, cap); r = M.Rast(POPPINS+f, px)
        if r.ink_h('H') != cap: continue
        print(f"  {lbl:8s} cap {cap:2d}  ({px} px em)  partial = {M.partial_fraction(r, ALNUM):.2f}")
print("\n\nSection 5 -- pairs at a 14 px capital, SemiBold\n")
def dist(a,b):
    if a is None or b is None: return None
    (la,ta,xa,_),(lb,tb,xb,_)=a,b
    if la.size==0 or lb.size==0: return None
    top=max(ta,tb); left=min(xa,xb)
    H=top+max(la.shape[0]-ta,lb.shape[0]-tb); W=max(xa+la.shape[1],xb+lb.shape[1])-left
    A=np.zeros((H,W)); B=np.zeros((H,W))
    A[top-ta:top-ta+la.shape[0], xa-left:xa-left+la.shape[1]]=la
    B[top-tb:top-tb+lb.shape[0], xb-left:xb-left+lb.shape[1]]=lb
    return float(abs(A-B).sum()/max(A.sum(),B.sum()))
PAIRS=[("I","1"),("l","1"),("S","5"),("Z","2"),("G","6"),("O","0"),("I","l"),("a","o")]
PICK=None  # chosen below: every face whose SemiBold hits a 14 px capital exactly
byname={n:(r,b) for n,r,b in CANDS}
cache={}
for n,_,sb in CANDS:
    r=M.Rast(sb, px_for_cap(sb,14))
    if r.ink_h('H')==14: cache[n]=r
PICK=list(cache)
print("  faces whose SemiBold reaches a 14 px capital at an integer size:", len(PICK), "of 16")
print("  (Roboto Condensed and Barlow SemiCondensed step 13 -> 15 and cannot appear here)\n")
print(f"{'pair':7s}" + "".join(f"{n[:13]:>14s}" for n in PICK))
for a,b in PAIRS:
    print(f"{a}/{b:5s}" + "".join(f"{dist(cache[n].glyph(a),cache[n].glyph(b)):14.2f}" for n in PICK))
