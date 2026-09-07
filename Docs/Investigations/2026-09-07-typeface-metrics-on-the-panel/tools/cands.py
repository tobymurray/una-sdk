"""The sixteen faces, and where the two path dependencies point.

POPPINS_DIR must hold Poppins-Regular.ttf and Poppins-SemiBold.ttf. They are not
copied here: they are OFL, but they are also the exact blobs the watch's own text
pipeline rasterizes, and the copy that matters is the one in that tree. Default is
a watch-apps checkout beside this one.

static/ holds the weight instances instance.py cuts from the variable fonts that
download.sh fetches; fonts/ holds those variable fonts and the two static Firas.
"""
import os

POPPINS = os.environ.get("POPPINS_DIR",
                         os.path.expanduser("~/git/watch-apps/TextKit/Fonts")) + "/"
ST = os.environ.get("STATIC_DIR", "static") + "/"
FN = os.environ.get("FONTS_DIR", "fonts") + "/"

CANDS = [
 ("Poppins",        POPPINS+"Poppins-Regular.ttf",        POPPINS+"Poppins-SemiBold.ttf"),
 ("Inter",          ST+"Inter-Regular.ttf",               ST+"Inter-SemiBold.ttf"),
 ("Inter Tight",    ST+"InterTight-Regular.ttf",          ST+"InterTight-SemiBold.ttf"),
 ("Roboto",         ST+"Roboto-Regular.ttf",              ST+"Roboto-SemiBold.ttf"),
 ("Roboto Cond",    ST+"RobotoCondensed-Regular.ttf",     ST+"RobotoCondensed-SemiBold.ttf"),
 ("Fira Sans",      FN+"FiraSans-Regular.ttf",            FN+"FiraSans-SemiBold.ttf"),
 ("IBM Plex Sans",  ST+"IBMPlexSans-Regular.ttf",         ST+"IBMPlexSans-SemiBold.ttf"),
 ("Public Sans",    ST+"PublicSans-Regular.ttf",          ST+"PublicSans-SemiBold.ttf"),
 ("Noto Sans",      ST+"NotoSans-Regular.ttf",            ST+"NotoSans-SemiBold.ttf"),
 ("Source Sans 3",  ST+"SourceSans3-Regular.ttf",         ST+"SourceSans3-SemiBold.ttf"),
 ("Open Sans",      ST+"OpenSans-Regular.ttf",            ST+"OpenSans-SemiBold.ttf"),
 ("Barlow",         FN+"Barlow-Regular.ttf",              FN+"Barlow-SemiBold.ttf"),
 ("Barlow SemiCond",FN+"BarlowSemiCondensed-Regular.ttf", FN+"BarlowSemiCondensed-SemiBold.ttf"),
 ("Archivo",        ST+"Archivo-Regular.ttf",             ST+"Archivo-SemiBold.ttf"),
 ("Manrope",        ST+"Manrope-Regular.ttf",             ST+"Manrope-SemiBold.ttf"),
 ("Montserrat",     ST+"Montserrat-Regular.ttf",          ST+"Montserrat-SemiBold.ttf"),
]
