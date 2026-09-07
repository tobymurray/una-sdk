"""Does atlas.py's load flag use the font's own bytecode, or the autohinter?

If forcing the autohinter changes the bitmap, the light path was using the font's
own instructions for that font -- so a hinted face is not on the same pipeline
Poppins is, and TEXT.md's threshold and parity evidence would not transfer.
"""
import freetype, numpy as np, measure as M
from cands import CANDS
import os
LIGHT = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT
NOAUTO = LIGHT | freetype.FT_LOAD_NO_AUTOHINT
CH = "HnaoOe0123ABCg"
def render(path, px, flags, ch):
    f = freetype.Face(path); f.set_pixel_sizes(0, px)
    f.load_glyph(f.get_char_index(ord(ch)), flags)
    b = f.glyph.bitmap
    if not (b.rows and b.width): return None
    cov = np.frombuffer(bytes(b.buffer), np.uint8).reshape(b.rows, b.pitch)[:, :b.width]
    return M.crop(M.quantise(cov))[0]
print(f"{'font':17s} {'fpgm':>4s}  glyphs of {len(CH)} that differ with/without the autohinter, at 12/16/18 px")
for name, reg, sb in CANDS:
    if not os.path.exists(reg): continue
    from fontTools.ttLib import TTFont
    tt = TTFont(reg); has = "fpgm" in tt and len(tt["fpgm"].program.getBytecode())>0
    diffs=[]
    for px in (12,16,18):
        n=0
        for ch in CH:
            a=render(reg,px,LIGHT,ch); b=render(reg,px,NOAUTO,ch)
            if a is None or b is None: continue
            if a.shape!=b.shape or not (a==b).all(): n+=1
        diffs.append(n)
    print(f"{name:17s} {'yes' if has else 'no ':>4s}  {diffs}")
