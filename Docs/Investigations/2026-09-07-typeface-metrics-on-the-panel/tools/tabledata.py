import measure as M, json, os
from fontTools.ttLib import TTFont
from cands import CANDS
from allsheets import px_for_cap
PAIRS=[("a","o"),("I","l")]
def dist(a,b):
    import numpy as np
    if a is None or b is None: return None
    (la,ta,xa,_),(lb,tb,xb,_)=a,b
    if la.size==0 or lb.size==0: return None
    top=max(ta,tb); left=min(xa,xb)
    H=top+max(la.shape[0]-ta,lb.shape[0]-tb); W=max(xa+la.shape[1],xb+lb.shape[1])-left
    A=np.zeros((H,W)); B=np.zeros((H,W))
    A[top-ta:top-ta+la.shape[0], xa-left:xa-left+la.shape[1]]=la
    B[top-tb:top-tb+lb.shape[0], xb-left:xb-left+lb.shape[1]]=lb
    return float(abs(A-B).sum()/max(A.sum(),B.sum()))
rows=[]
for name, reg, sb in CANDS:
    tt=TTFont(reg); cap=tt["OS/2"].sCapHeight; cm=tt.getBestCmap(); hm=tt["hmtx"]
    dwid=sum(hm[cm[ord(c)]][0] for c in "0123456789")/10/cap
    greek=sum(1 for cp in range(0x370,0x400) if cp in cm)
    cyr=sum(1 for cp in range(0x400,0x500) if cp in cm)
    heart=0x2665 in cm; prime=0x2032 in cm
    pc=px_for_cap(sb,42); rc=M.Rast(sb,pc)
    clock=rc.advance("08:42"); ccap=rc.ink_h('H')
    p9=px_for_cap(reg,9); r9=M.Rast(reg,p9)
    ao=dist(r9.glyph('a'),r9.glyph('o')); il=dist(r9.glyph('I'),r9.glyph('l'))
    op,tot,closed=M.counter_report(r9)
    rows.append(dict(name=name, digit=round(dwid,3), clock=round(clock*42/ccap),
                     ao=round(ao,2), il=round(il,2), greek=greek, cyr=cyr,
                     heart=heart, prime=prime, counters=f"{op}/{tot}", closed=closed,
                     solid=round(M.solid_fraction(r9,"0123456789"),2)))
json.dump(rows, open("table.json","w"), indent=1)
print(json.dumps(rows, indent=0)[:900])
