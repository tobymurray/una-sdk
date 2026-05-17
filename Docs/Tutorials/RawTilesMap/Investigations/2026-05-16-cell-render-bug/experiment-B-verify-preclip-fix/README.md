# Experiment B — verify the suggested src-rect pre-clip fix

**Date**: 2026-05-16  
**Goal**: confirm Experiment A's hypothesis that the bug is negative target X handling, and test the fix proposed at the bottom of [experiment A's README](../experiment-A-four-distinct-colors/README.md): pre-clip each cell's src rect so target X/Y are non-negative.

## Setup

- Same 4-color fabricated buffers as Experiment A (RED/GREEN/BLUE/YELLOW at slots (1,1)/(2,1)/(1,2)/(2,2)).
- `TileCanvas::draw` modified so, for every cell, when `cellLocalX < 0`: advance `src.x` by `-cellLocalX`, shrink `src.width` correspondingly, set `target.x = 0`. Same treatment for Y.
- Effective per-cell draw parameters after the pre-clip:

| Cell  | Original target | Pre-clipped target | Pre-clipped src rect (x, y, w, h) |
|---|---|---|---|
| (1,1) RED    | (-136, -136) | (0,   0)   | (136, 136, 120, 120) |
| (2,1) GREEN  | (120,  -136) | (120, 0)   | (0,   136, 256, 120) |
| (1,2) BLUE   | (-136, 120)  | (0,   120) | (136, 0,   120, 256) |
| (2,2) YELLOW | (120,  120)  | (120, 120) | (0,   0,   256, 256) — unchanged |

Patch applied: see [`patch.diff`](patch.diff).

## Predicted vs observed

**Predicted** (if Experiment A's hypothesis was complete and the fix was correct): four 120×120 colored quadrants — RED in TL, GREEN in TR, BLUE in BL, YELLOW in BR.

**Observed** (see [`screenshots/result.png`](screenshots/result.png)):

| Widget quadrant         | Expected color | Actual color |
|---|---|---|
| TL (0..120, 0..120)     | RED            | **black/slate (empty)** |
| TR (120..240, 0..120)   | GREEN          | **black/slate (empty)** |
| BL (0..120, 120..240)   | BLUE           | **YELLOW** |
| BR (120..240, 120..240) | YELLOW         | YELLOW |

Registration log is identical to Experiment A's (same IDs 16..19, same stored addresses, same first-byte verification). Full log: [`logs/run.log`](logs/run.log).

## Updated bug hypothesis

The pre-clip fix did *not* fix any of the negative-X cells. Worse — it broke (2,1) GREEN, which was rendering at full top width before. The only cell that still renders is (2,2) YELLOW, the one cell whose src rect was *not* modified (because both its target X and target Y are already positive).

So the actual rule the prebuilt `LCD8bpp_ABGR2222::drawPartialBitmap` appears to follow for external dynamic bitmaps in this build is *stricter* than Experiment A suggested:

- **A draw is dropped silently if `src.width != bitmap.width` OR `src.height != bitmap.height`.**  
  (Cells (1,1), (1,2), and (2,1) all had cropped src rects after the pre-clip; all three vanished.)
- **A draw with full src rect (`src.x=0, src.y=0, src.width=bitmap.width, src.height=bitmap.height`) and positive target X renders the bitmap stretched/positioned at `target.y` across the full widget width**, ignoring `target.x` (or treating it as 0).
- **A draw with full src rect and negative target X is dropped silently.**

In other words: the `Rect` argument to `drawPartialBitmap` (which the API doc claims is the source sub-rectangle) appears to be ignored entirely, *and* the framework throws away the draw whenever the rect would have been a sub-rectangle. The function effectively only handles "draw the entire bitmap at (any positive Y) and span the widget width."

This narrows the open hypotheses considerably. The earlier idea that we could work around it by passing a cropped src rect is dead. Possible workarounds:

1. **Use `LCD::blitCopy`** (mentioned as a fallback in TileCanvas.cpp's diagnostic comment at commit `f61740d`). A lower-level primitive that doesn't go through `drawPartialBitmap`'s broken path.
2. **Pre-compose the visible quadrants into a single full-widget bitmap** server-side: allocate one 240×240 ABGR2222 buffer, blit the four cell quadrants into it manually, register *that* with `dynamicBitmapCreateExternal`, and draw it once with a single drawPartialBitmap at (0, 0) and src = full buffer. Slow but works around the framework's restrictions.
3. **Use static bitmaps from `BitmapDatabase`** instead of dynamic external. Static bitmaps render correctly in this build (the L/R button overlays prove this). The downside is no runtime substitution; would need a tile-cache approach with imageconvert-style preprocessing.

## What this tells us about the parent investigation

The original RawTilesMap tutorial's design — register N dynamic external bitmaps for the visible viewport, draw each at a per-cell offset using `drawPartialBitmap` with the full src rect — collides head-on with this framework limitation. The seam-centered mosaic *can't* work via `drawPartialBitmap` as currently called: cells at col=1 (negative X target) and any cell with a partial src rect will be dropped.

The fact that **earlier commits (`44ab360`, `bc431d9`) saw a "rotated synthetic pattern" rendering** is consistent with this hypothesis: in those commits all 4 cells used `sDebugPattern` *and* the full src rect, so the framework drew *one* of the cells stretched across each row of the widget. The "rotation" the user observed was actually two cells rendering and three not rendering, producing a half-stretched pattern that looked rotated relative to the design intent.

## Suggested next experiments

In order of expected information yield:

1. **Experiment C: try `LCD::blitCopy`** for one of the cells (e.g. (1,1)) and see if it renders at the correct widget position with the correct sub-rect of the bitmap. If yes, the workaround is to bypass `drawPartialBitmap` entirely for the mosaic.
2. **Experiment D: pre-compose into one 240×240 buffer**. Allocate one buffer, manually copy the four cell quadrants into it (with the same offset math currently in `TileCanvas::draw`), register it once, draw once at (0, 0) with src = full buffer. Should produce the four colored quadrants. Confirms the API limitation rather than working around it.
3. **Experiment E: static-bitmap comparison**. Drop a single full-bitmap `touchgfx::Image` widget with one of the imageconvert-generated bitmaps (e.g. `BITMAP_HEART_ID`) and confirm it renders with partial src rect via `drawPartialBitmap`. If static works and dynamic-external doesn't, the bug is specifically in the external-dynamic path.
4. **Disassemble `LCD8bpp_ABGR2222::drawPartialBitmap`** now that we have a precise spec to test against ("does it honor the `rect` parameter").

## Reproducing this experiment

Apply [`patch.diff`](patch.diff), build, and launch with `RAWTILES_PATH` set (any non-empty pack works; the bytes aren't used for rendering). Screenshot recipe in [`Docs/Simulator-Linux.md`](../../../../../Simulator-Linux.md#capturing-a-simulator-screenshot).
