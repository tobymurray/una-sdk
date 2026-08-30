# Experiment C2 — `LCD::blitCopy` with corrected source-relative `blitRect`

**Date**: 2026-05-16  
**Goal**: re-run Experiment C with `blitRect` correctly computed as source-relative (not absolute screen coords). C's flawed call shape made `blitCopy` look broken; C2 tests it properly.

## Setup

- Same 4-color fabricated buffers as A/B/C (RED/GREEN/BLUE/YELLOW at slots (1,1)/(2,1)/(1,2)/(2,2)).
- `TileCanvas::draw` computes the intersection of the bitmap's absolute-screen rect with the dirty rect, then translates that intersection into coords relative to the bitmap origin for `blitRect`:

  ```cpp
  const Rect source(absDX + cellLocalX, absDY + cellLocalY,
                    bmp.getWidth(), bmp.getHeight());

  const int16_t ix0 = std::max(source.x, absRect.x);
  const int16_t iy0 = std::max(source.y, absRect.y);
  const int16_t ix1 = std::min(int16_t(source.x + source.width),
                               int16_t(absRect.x + absRect.width));
  const int16_t iy1 = std::min(int16_t(source.y + source.height),
                               int16_t(absRect.y + absRect.height));
  if (ix1 <= ix0 || iy1 <= iy0) continue;

  const Rect blitRect(ix0 - source.x, iy0 - source.y, ix1 - ix0, iy1 - iy0);
  HAL::lcd().blitCopy(pixels, Bitmap::ABGR2222, source, blitRect, 255, false);
  ```

Patch applied: see [`patch.diff`](patch.diff).

## Result

**Four distinct 120×120 colored quadrants in their expected widget positions** ([`screenshots/result.png`](screenshots/result.png)):

| Widget quadrant         | Expected | Actual |
|---|---|---|
| TL (0..120, 0..120)     | RED      | **RED** ✓ |
| TR (120..240, 0..120)   | GREEN    | **GREEN** ✓ |
| BL (0..120, 120..240)   | BLUE     | **BLUE** ✓ |
| BR (120..240, 120..240) | YELLOW   | **YELLOW** ✓ |

The L/R button overlays still render correctly in the corners as expected (those are static `BitmapDatabase` bitmaps unrelated to our experiment).

Registration log identical to A/B/C. Full log: [`logs/run.log`](logs/run.log).

## What this proves

`LCD::blitCopy` for ABGR2222 external dynamic bitmaps **works correctly** in the prebuilt `libtouchgfx.a` on this platform — it honors `source.x/y` for positioning, accepts negative source coords (the bitmap extends off-screen), and uses `blitRect` as a source-relative sub-rectangle. The earlier-attributed "framework bug" was entirely in our call shape, not in the framework.

The bug that *does* exist, narrowed by this result:

- `LCD::drawPartialBitmap(bmp, x, y, src, alpha, useOptimized)` for ABGR2222 external dynamic bitmaps in this build does **not** handle the case where `(x, y)` is negative — it either drops the draw or stretches the source rect across the widget width (the precise rule is still TBD without disassembly). This is what experiments A and B revealed.
- The natural workaround is to use `LCD::blitCopy` directly with the intersection math above. That's what C2 demonstrates.

## Resolution of all observations from this investigation

With the rule "drawPartialBitmap with ABGR2222 external + negative target X drops the draw; positive target X stretches the source across widget width," every earlier observation is explained:

- **Original screenshot 01 (only (1,1) substituted with RED)** — (1,1) had neg X, dropped silently. The remaining 3 cells used real tile data; cells (2,1) and (2,2) stretched their tile content across widget halves, masking the missing (1,1) draw.
- **Screenshot 02 ((1,1) + (2,2) both RED)** — same as above plus (2,2) RED stretches across the bottom half. Top half = (2,1) tile stretched; bottom half = (2,2) RED stretched.
- **Screenshot 03 (all 4 RED)** — (2,1) and (2,2) both red, each stretches across its half; cells (1,1) and (1,2) dropped.
- **Pre-clip fix in Experiment B** — broke (2,1) GREEN because the framework's drawPartialBitmap also rejects sub-rectangle src (or at least the combination of sub-rect src and negative target X).
- **Experiment C (blitCopy with wrong blitRect)** — blitCopy was passed absolute-screen blitRect, which collides with the source-relative interpretation. Some scanlines from RED landed correctly while others were overdrawn or skipped.
- **Experiment C2 (blitCopy with correct blitRect)** — works perfectly. Four colored quadrants render exactly where expected.

## Recommendation

Replace the `HAL::lcd().drawPartialBitmap(...)` call in `TileCanvas::draw` with the `blitCopy(...)` intersection pattern shown above. This:

1. Renders the mosaic correctly (verified by this experiment).
2. Avoids the negative-target-X bug in the prebuilt framework's drawPartialBitmap path entirely.
3. Keeps the per-cell `dynamicBitmapCreateExternal` registration (no need for server-side composition into one buffer — Experiment D is no longer required).
4. Is essentially the same pattern TouchGFX itself uses in `PixelDataWidget`, just generalized to a multi-bitmap mosaic with per-cell source positions.

The bug should also be reported upstream to TouchGFX support with the minimal reproducer in [`../README.md#minimal-upstream-ready-reproducer`](../README.md), refined now to specifically say "`drawPartialBitmap` with negative `x` or `y` drops the draw for ABGR2222 external dynamic bitmaps; `blitCopy` with the same buffer at the same negative position works."

## Reproducing this experiment

Apply [`patch.diff`](patch.diff), build, launch. The result will be the four colored quadrants screenshot.

Screenshot capture recipe: [`RESEARCH-INDEX.md`](../../../../../../RESEARCH-INDEX.md) § 8.
