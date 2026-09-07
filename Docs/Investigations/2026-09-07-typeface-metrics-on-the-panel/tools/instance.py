import os, sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

SPECS = [
    # file, outname, {axis: value}
    ("Inter-var.ttf", "Inter-Regular", {"wght":400, "opsz":14}),
    ("Inter-var.ttf", "Inter-SemiBold", {"wght":600, "opsz":14}),
    ("Inter-var.ttf", "InterDisplay-SemiBold", {"wght":600, "opsz":32}),
    ("InterTight-var.ttf", "InterTight-Regular", {"wght":400}),
    ("InterTight-var.ttf", "InterTight-SemiBold", {"wght":600}),
    ("Roboto-var.ttf", "Roboto-Regular", {"wght":400, "wdth":100}),
    ("Roboto-var.ttf", "Roboto-SemiBold", {"wght":600, "wdth":100}),
    ("RobotoCondensed-var.ttf", "RobotoCondensed-Regular", {"wght":400}),
    ("RobotoCondensed-var.ttf", "RobotoCondensed-SemiBold", {"wght":600}),
    ("IBMPlexSans-var.ttf", "IBMPlexSans-Regular", {"wght":400, "wdth":100}),
    ("IBMPlexSans-var.ttf", "IBMPlexSans-SemiBold", {"wght":600, "wdth":100}),
    ("PublicSans-var.ttf", "PublicSans-Regular", {"wght":400}),
    ("PublicSans-var.ttf", "PublicSans-SemiBold", {"wght":600}),
    ("NotoSans-var.ttf", "NotoSans-Regular", {"wght":400, "wdth":100}),
    ("NotoSans-var.ttf", "NotoSans-SemiBold", {"wght":600, "wdth":100}),
    ("SourceSans3-var.ttf", "SourceSans3-Regular", {"wght":400}),
    ("SourceSans3-var.ttf", "SourceSans3-SemiBold", {"wght":600}),
    ("OpenSans-var.ttf", "OpenSans-Regular", {"wght":400, "wdth":100}),
    ("OpenSans-var.ttf", "OpenSans-SemiBold", {"wght":600, "wdth":100}),
    ("Montserrat-var.ttf", "Montserrat-Regular", {"wght":400}),
    ("Montserrat-var.ttf", "Montserrat-SemiBold", {"wght":600}),
    ("Archivo-var.ttf", "Archivo-Regular", {"wght":400, "wdth":100}),
    ("Archivo-var.ttf", "Archivo-SemiBold", {"wght":600, "wdth":100}),
    ("Manrope-var.ttf", "Manrope-Regular", {"wght":400}),
    ("Manrope-var.ttf", "Manrope-SemiBold", {"wght":600}),
]
os.makedirs("static", exist_ok=True)
for src, name, loc in SPECS:
    p = os.path.join("fonts", src)
    if not os.path.exists(p):
        print("missing", src); continue
    try:
        f = TTFont(p)
        inst = instancer.instantiateVariableFont(f, loc, inplace=False, updateFontNames=False)
        out = os.path.join("static", name + ".ttf")
        inst.save(out)
        print("ok", name, os.path.getsize(out))
    except Exception as e:
        print("FAIL", name, type(e).__name__, e)
