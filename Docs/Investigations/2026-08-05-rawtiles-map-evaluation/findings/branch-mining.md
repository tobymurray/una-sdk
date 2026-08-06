# Branch + commit-history mining (TouchGFX bug branches, experiments lineage, pack provenance)

Source: background research agent, 2026-08-05. All read-only.

## TouchGFX drawPartialBitmap branches
- `docs/touchgfx-drawpartialbitmap-negative-x`: 1 commit `9c59b21d`, +331/-3, adds `Libs/Header/SDK/GUI/DynamicBitmapDraw.hpp` (+154) and `Docs/Investigations/2026-05-16-touchgfx-drawpartialbitmap-negative-x/README.md`. Bug: drawPartialBitmap "silently drops ABGR2222 dynamic bitmaps if the target x is negative, and ignores x when it's positive". (Unprefixed `touchgfx-drawpartialbitmap-negative-x` is a 3-commit superset rebase.)
- `docs/touchgfx-drawpartialbitmap-y-clipping-bug-report`: 4 commits (head `ff098549`), adds `Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md` (at `Docs/`, NOT `Docs/Investigations/`). Distinct second defect: overhanging rows written at `dirtyRect.y` instead of `screenY`; "X-axis clipping is correct... The defect is specific to vertical overhang."
- `bug/drawpartialbitmap-y-clipping-repro`: 1 commit `f5bfa69a`, +65, HelloWorld repro widget at (60,220); carries no workaround header.

CONFIRMED: `DynamicBitmapDraw.hpp` workaround gate is exactly `#if defined(SIMULATOR) && defined(__linux__)` (line 110, `#else` 142, `#endif` 148) on both branches carrying it.
CONFIRMED: hardware reproduction — `Docs/touchgfx-drawpartialbitmap-y-clipping-bug-report.md:36` @ `ff098549`: "Reproduced on Windows simulator (MSVC), Linux simulator, and STM32U595 hardware. Same TouchGFX source across all three, so this is not platform-specific."
CONFIRMED mismatch, intra-branch: `ff098549` lands the hardware-repro doc while the header on the same branch compiles the workaround out for embedded ARM — hardware gets the exact drawPartialBitmap the doc calls broken. The README's own remedy (extend the #if per platform) was never applied. Also: ABGR2222 hard-coded in the blitCopy dispatch; the workaround targets negative-X only — nothing addresses Y-overhang.

## Five load-bearing lessons from the ~45-commit experiments lineage
1. `7ce54067` — negative anchors are a framebuffer-underrun SIGSEGV, not a visual bug.
2. `081c16ab` / `5381d01e` — anchor draws at the bitmap origin, not the clip-rect corner.
3. `19422afe` — seam centre must be the first index entry at zoom_max, not the median; median hits `cx_max` on even-x packs and blacks out the right half.
4. `99471a73` / `dfbc3ddf` — dynamic bitmaps skip imageconvert's pre-rotation → content renders 90° CW; fixed by committing to landscape end-to-end.
5. `b07d33a4`→`e8a7eaf2`→`9712431d` — the blitCopy pivot; first attempt failed on an absolute rather than source-relative blitRect and was nearly abandoned for server-side composition.
RAM headline from the lineage: 96 KB → 640 KB for the 3×3 viewport; zero-decode memcpy (pack bytes are TouchGFX-native ABGR2222) is the perf keystone.

## Pack provenance
CONFIRMED: `8897daf7` (182 files, +24,186) commit body: "Resources/stanley.rawtiles — ships a slippypack-generated sample pack (Stanley Park, Vancouver: 12 tiles at z=12..14, 769 KB ABGR2222)". Corroborated by `6c9ab613`. No slippypack exists anywhere public.
Unreconciled discrepancy: 12 tiles at z=12..14 (8897daf7) vs `19422afe`'s "2 columns × 3 rows at z=14".
