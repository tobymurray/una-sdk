import measure as M, numpy as np
from cands import CANDS
from allsheets import px_for_cap
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
PAIRS=[("I","1"),("O","0"),("I","l"),("l","1"),("S","5"),("Z","2"),("B","8"),("G","6")]
byname={n:(r,b) for n,r,b in CANDS}
for role, cap, wi in [("LARGE, the preferred id face (Poppins SemiBold 20, cap 14)",14,1),
                      ("SMALL, split ids + caption + prompts (Poppins Regular 18, cap 13)",13,0)]:
    print("="*96); print(role); print("="*96)
    print(f"{'face':17s} " + "".join(f"{a}/{b:<5s}" for a,b in PAIRS))
    for n in ["Poppins","Source Sans 3","IBM Plex Sans","Fira Sans","Noto Sans","Open Sans","Inter Tight","Roboto Cond"]:
        p=byname[n][wi]; px=px_for_cap(p,cap); r=M.Rast(p,px)
        ds=[dist(r.glyph(a),r.glyph(b)) for a,b in PAIRS]
        print(f"{n:17s} " + "".join(f"{'  -  ':7s}" if v is None else f"{v:5.2f}  " for v in ds))
    print()
