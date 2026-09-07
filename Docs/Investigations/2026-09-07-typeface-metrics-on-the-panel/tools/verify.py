import measure as M
from cands import POPPINS as F
for font,px,expect_total in [("Poppins-SemiBold.ttf",18,3778),("Poppins-Regular.ttf",14,2723),
                             ("Poppins-Regular.ttf",12,2359),("Poppins-SemiBold.ttf",20,4471)]:
    r=M.Rast(F+font,px)
    n,data,total=r.atlas_bytes()
    print(f"{font} {px}: {n} glyphs, {data} glyph bytes, {total} total (README {expect_total}) {'OK' if total==expect_total else 'MISMATCH'}")
