#!/bin/sh
# Fetches the fifteen non-Poppins candidates. Poppins is not fetched: POPPINS_DIR
# in cands.py points at the copy the watch's own pipeline rasterizes.
set -e
mkdir -p fonts && cd fonts
B=https://raw.githubusercontent.com/google/fonts/main
get() { curl -sfL -o "$2" "$B/$1" && echo "  $2"; }
get 'ofl/inter/Inter%5Bopsz,wght%5D.ttf'                          Inter-var.ttf
get 'ofl/intertight/InterTight%5Bwght%5D.ttf'                     InterTight-var.ttf
get 'ofl/roboto/Roboto%5Bwdth,wght%5D.ttf'                        Roboto-var.ttf
get 'ofl/robotocondensed/RobotoCondensed%5Bwght%5D.ttf'           RobotoCondensed-var.ttf
get 'ofl/ibmplexsans/IBMPlexSans%5Bwdth,wght%5D.ttf'              IBMPlexSans-var.ttf
get 'ofl/publicsans/PublicSans%5Bwght%5D.ttf'                     PublicSans-var.ttf
get 'ofl/notosans/NotoSans%5Bwdth,wght%5D.ttf'                    NotoSans-var.ttf
get 'ofl/sourcesans3/SourceSans3%5Bwght%5D.ttf'                   SourceSans3-var.ttf
get 'ofl/opensans/OpenSans%5Bwdth,wght%5D.ttf'                    OpenSans-var.ttf
get 'ofl/montserrat/Montserrat%5Bwght%5D.ttf'                     Montserrat-var.ttf
get 'ofl/archivo/Archivo%5Bwdth,wght%5D.ttf'                      Archivo-var.ttf
get 'ofl/manrope/Manrope%5Bwght%5D.ttf'                           Manrope-var.ttf
get 'ofl/firasans/FiraSans-Regular.ttf'                           FiraSans-Regular.ttf
get 'ofl/firasans/FiraSans-SemiBold.ttf'                          FiraSans-SemiBold.ttf
get 'ofl/barlow/Barlow-Regular.ttf'                               Barlow-Regular.ttf
get 'ofl/barlow/Barlow-SemiBold.ttf'                              Barlow-SemiBold.ttf
get 'ofl/barlowsemicondensed/BarlowSemiCondensed-Regular.ttf'     BarlowSemiCondensed-Regular.ttf
get 'ofl/barlowsemicondensed/BarlowSemiCondensed-SemiBold.ttf'    BarlowSemiCondensed-SemiBold.ttf
cd .. && python3 instance.py
