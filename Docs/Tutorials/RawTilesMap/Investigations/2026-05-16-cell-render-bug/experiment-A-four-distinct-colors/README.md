# Experiment A — four distinct solid-color buffers

**Date**: 2026-05-16  
**Goal**: settle whether the cell-(1,1) substitution bug is mixed-source confusion, shared-pointer count, slot indexing, or coordinate transform. Bypass the rawtiles pack entirely and hand each of the four visible viewport slots its own fabricated 256×256 ABGR2222 buffer with a unique solid color. The screenshot directly reports which BitmapId lands in which widget quadrant.

## Setup

| Slot (col, row) | Buffer | Pixel byte | Expected widget quadrant |
|---|---|---|---|
| (1, 1) | `sExpA_Red` (256×256, all `0xC3`)    | 0xC3 — opaque red    | TL (0..120, 0..120) |
| (2, 1) | `sExpA_Green` (256×256, all `0xCC`)  | 0xCC — opaque green  | TR (120..240, 0..120) |
| (1, 2) | `sExpA_Blue` (256×256, all `0xF0`)   | 0xF0 — opaque blue   | BL (0..120, 120..240) |
| (2, 2) | `sExpA_Yellow` (256×256, all `0xCF`) | 0xCF — opaque yellow | BR (120..240, 120..240) |

- All buffers are static-BSS `uint8_t[65536]` arrays, naturally aligned.
- `Bitmap::setCache(pool=4 KB, numberOfDynamicBitmaps=4)` is called once.
- Each buffer is registered via `dynamicBitmapCreateExternal(256, 256, buf, Bitmap::ABGR2222)`.
- Returned `BitmapId`s are stored in `mViewport.ids[row*3 + col]`.
- `TileCanvas::draw` is unchanged from `1c94d8b`: target = `((col-2)*256 + halfW, (row-2)*256 + halfH)` with `halfW=halfH=120`, src = full bitmap, `useOptimized=true`.
- `setDisplayOrientation(ORIENTATION_LANDSCAPE)` is unchanged. Display is 240×240 square.

Patch applied to `Model.cpp`: see [`patch.diff`](patch.diff).

## Predicted vs observed

**Predicted** (assuming the framework respects target position and per-bitmap pixel pointer): four 120×120 colored quadrants — RED in TL, GREEN in TR, BLUE in BL, YELLOW in BR.

**Observed** (see [`screenshots/result.png`](screenshots/result.png)):

| Widget quadrant | Expected color | Actual color |
|---|---|---|
| TL (0..120, 0..120)     | RED    | **GREEN** |
| TR (120..240, 0..120)   | GREEN  | GREEN |
| BL (0..120, 120..240)   | BLUE   | **YELLOW** (with a thin BLUE sliver at the y=120 seam) |
| BR (120..240, 120..240) | YELLOW | YELLOW |

RED and BLUE — the two buffers handed to slots whose draw-target X coordinate is **negative** (cellLocalX = -136 for col=1) — never appear on screen. GREEN and YELLOW — the two buffers whose draw-target X is **positive** (cellLocalX = +120 for col=2) — render at full widget width (X=0 to X=240), not just their assigned half.

The registration side is healthy:

```
EXP-A: slot (col=1,row=1) RED    id=16 buf=0x4f3fc0 stored=0x4f3fc0 first_byte=0xc3 expected=0xc3
EXP-A: slot (col=2,row=1) GREEN  id=17 buf=0x503fc0 stored=0x503fc0 first_byte=0xcc expected=0xcc
EXP-A: slot (col=1,row=2) BLUE   id=18 buf=0x513fc0 stored=0x513fc0 first_byte=0xf0 expected=0xf0
EXP-A: slot (col=2,row=2) YELLOW id=19 buf=0x523fc0 stored=0x523fc0 first_byte=0xcf expected=0xcf
```

Distinct IDs, distinct buffer addresses, `stored == buf` for all four, and the byte at each stored address matches the expected color. Full log: [`logs/run.log`](logs/run.log).

## Key finding

**The bug is `drawPartialBitmap` mishandling negative target X (not negative Y, not slot (1,1) specifically, not first-registered, not mixed-source, not shared-pointer-count).** Cells drawn with `target.x < 0` are dropped silently. Cells drawn with `target.x >= 0` render their *full* bitmap source rect spanning widget X = 0 to widget X = widget width, ignoring the target X offset (or treating it as 0).

This single hypothesis collapses every earlier observation:

- **Single-cell substitute at (1,1)** — never visible because (1,1) is the only cell with negative X *and* row-1 cells (negative Y) happen to be drawn first, so cells (2,1)/(2,2) overdraw the entire top/bottom halves regardless. The "real tile content" we saw at widget TL was actually cell (2,1)'s tile spanning the full width.
- **Diagonal (1,1)+(2,2) substitute** — RED at (1,1) invisible; tile at (2,1) covers full top half; tile at (1,2) invisible; RED at (2,2) covers full bottom half. Matches "top tile, bottom red".
- **All 4 substituted** — RED at (1,1) and (1,2) invisible; RED at (2,1) covers top half; RED at (2,2) covers bottom half. Matches "all red".
- **Pre-clipped to positive target** — when I changed cell (1,1)'s target to (0, 0) with src=(136,136,120,120), cell (1,1) *did* attempt to render at widget (0, 0), but the other cells' src/target rewriting also broke their math, producing the garbled output in `08-pre-clipped-positive-target-WEIRD-NO-PATTERN.png`. With the correct fix applied uniformly (see next steps), this approach should work.

## Why earlier debugging didn't catch this

Because the four cells in the original setup all had *visually compatible* tile content (a coherent OSM map across cells (1,1)..(2,2)), the X-stretch artifact was invisible — a top-half stretched to full width still looked like a coherent map because the map continues seamlessly across the seam. Only when each cell has a uniquely distinguishable solid color does the X-stretching become obvious.

The pre-clip experiment (`08-pre-clipped-positive-target-WEIRD-NO-PATTERN.png`) actually did contain the fix for cell (1,1) — but I broke the other three cells in the same patch, so the result looked broken overall and I drew the wrong conclusion.

## Suggested fix

In `TileCanvas::draw`, when `cellLocalX < 0`, advance the src rect's X origin by `-cellLocalX` and clamp `cellLocalX` to 0 (mirror the same fix for Y if `cellLocalY < 0`). Apply this consistently per-cell rather than only on the broken one. Specifically:

```cpp
int srcX = 0, srcY = 0;
int srcW = int(bmp.getWidth()), srcH = int(bmp.getHeight());
int16_t targetX = cellLocalX, targetY = cellLocalY;
if (cellLocalX < 0) { srcX = -cellLocalX; srcW -= srcX; targetX = 0; }
if (cellLocalY < 0) { srcY = -cellLocalY; srcH -= srcY; targetY = 0; }
const Rect src(int16_t(srcX), int16_t(srcY), int16_t(srcW), int16_t(srcH));
HAL::lcd().drawPartialBitmap(bmp, absDX + targetX, absDY + targetY, src, 255, true);
```

This is precisely what commit `e6682a7` (anchor at bitmap origin) and `f61740d` (rely on framework clipping) wrestled with — the framework's automatic clipping for negative targets is broken for ABGR2222 external dynamic bitmaps, so we have to do it ourselves.

## Status of the open hypotheses from the parent README

| Hypothesis | Status after Experiment A |
|---|---|
| SDL2 LCD caches `SDL_Surface` keyed by pixel-pointer | **Rejected.** Four distinct pointers all render (or fail to render) according to target X, not pointer identity. |
| `LCD8bpp_ABGR2222::drawPartialBitmap` mishandles mixed-source dynamic bitmaps in same frame | **Refined.** The bug is specifically negative-target-X handling in `drawPartialBitmap`, not "mixed-source". |
| `setDisplayOrientation(LANDSCAPE)` rotates static bitmaps but not external dynamic | **Still possible** — could be tested by reverting to PORTRAIT and seeing if negative target X starts working. |
| Dynamic bitmap cache pool corruption at specific fill ratio | **Rejected.** Four bitmaps in a 4 KB pool with `numberOfDynamicBitmaps=4` is well within capacity and renders consistently for positive-X cells. |

## Next experiments worth running

1. **Verify the suggested fix** — apply the manual src-rect pre-clip to all four cells uniformly in `TileCanvas::draw` and confirm 4 distinct quadrants appear with the experiment-A setup.
2. **Test with `ORIENTATION_PORTRAIT`** — if negative-target-X works in portrait, the bug is specific to the landscape transform path.
3. **Test single-bitmap `touchgfx::Image` widget** with a negative-X positioning — confirms the bug is in `drawPartialBitmap` itself and not in our canvas math.
4. **Disassemble `LCD8bpp_ABGR2222::drawPartialBitmap`** — now that we know what symptom to look for (negative target X handling), the disassembly is much easier to read.

## Reproducing this experiment

The Model.cpp patch is in [`patch.diff`](patch.diff). Apply with:

```bash
git apply patch.diff
UNA_SDK=$PWD make -C Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI -f simulator/gcc/Makefile -j$(nproc)
RAWTILES_PATH=$PWD/Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles \
  DISPLAY=:0.0 ./Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI/build/bin/simulator.out
```

The pack file is still opened (to derive `h.tileDimPx`) but its tile bytes are not used for rendering. The four fabricated buffers replace tile.data for the four visible slots; the other five slots remain `BITMAP_INVALID`.

Screenshot capture: see [`Docs/Simulator-Linux.md`](../../../../../Simulator-Linux.md#capturing-a-simulator-screenshot).
