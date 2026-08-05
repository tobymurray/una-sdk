# Experiment C — `LCD::blitCopy` instead of `drawPartialBitmap`

**Date**: 2026-05-16  
**Goal**: bypass `drawPartialBitmap` entirely by using the lower-level `LCD::blitCopy` primitive that the original `TileCanvas` diagnostic comment names as the fallback "if the framework doesn't clip far-negative anchors." Experiment B confirmed the framework's clipping/sub-rect support for `drawPartialBitmap` of ABGR2222 external dynamic bitmaps is broken; `blitCopy` is the next layer down.

## Setup

- Same 4-color fabricated buffers as experiments A/B (RED/GREEN/BLUE/YELLOW at slots (1,1)/(2,1)/(1,2)/(2,2)).
- `TileCanvas::draw` modified per [`patch.diff`](patch.diff). The drawPartialBitmap call is replaced with:

  ```cpp
  const uint8_t* pixels = Bitmap::dynamicBitmapGetAddress(id);
  const Rect source(absDX + cellLocalX, absDY + cellLocalY,
                    bmp.getWidth(), bmp.getHeight());
  HAL::lcd().blitCopy(pixels, Bitmap::ABGR2222, source, absRect, 255, false);
  ```

- This mirrors `PixelDataWidget::draw` from the TouchGFX framework: `source` describes the bitmap's absolute screen position+size; `absRect` (the dirty rect translated to absolute coords) is passed as `blitRect`. `useOptimized`-equivalent flag is `hasTransparentPixels=false`.

## Predicted vs observed

**Predicted** (if `blitCopy` honors `source.x/y` for positioning): four 120×120 colored quadrants. Bitmaps with negative source.x/y get clipped naturally by the LCD's framebuffer bounds.

**Observed** (see [`screenshots/result.png`](screenshots/result.png)):

| Widget quadrant | Expected | Actual |
|---|---|---|
| TL (0..120, 0..120)     | RED    | mostly green with red horizontal scanlines threading through and a small red icon-shape near center-top |
| TR (120..240, 0..120)   | GREEN  | green with the same red scanlines |
| BL (0..120, 120..240)   | BLUE   | **black/slate** (white/orange L/R button overlays in the corners are static bitmaps drawing correctly) |
| BR (120..240, 120..240) | YELLOW | **black/slate** |

The small red icon-shape near the top-center is almost certainly a *static* bitmap from `BitmapDatabase` rendering at its designed position (heart icon or one of the L/R button assets) — not our dynamic RED. The thin red horizontal scanlines threading through the green top half, however, are the interesting signal: blitCopy is writing *some* scanlines from the RED buffer, but most pixels in the top half end up GREEN (from cell (2,1)'s blitCopy).

Registration log is identical to experiments A and B (IDs 16..19, stored addresses match, expected bytes verified). Full log: [`logs/run.log`](logs/run.log).

### Caveat — blitCopy arguments may have been wrong

`PixelDataWidget::draw` passes `invalidatedArea` (in **widget-local** coords) as the `blitRect` argument. My TileCanvas Experiment C patch passed `absRect` (in **absolute screen** coords) instead. For a widget whose absolute position is (0, 0) — like the canvas widget here — those happen to numerically coincide, but they're conceptually different.

More importantly, `blitRect` is documented as "the region of the sourceData to copy", i.e. it should be **source-relative coords** (relative to `source.x, source.y`, where source.x/y is the bitmap origin in screen coords). I passed it as screen-absolute. For cells whose `source.x/y` is non-zero (which is all of them), this is wrong. The right calculation would be:

```cpp
// Intersection of bitmap-in-screen-coords with widget's absolute dirty rect
const int16_t x0 = std::max(source.x, absRect.x);
const int16_t y0 = std::max(source.y, absRect.y);
const int16_t x1 = std::min(int16_t(source.x + source.width),  int16_t(absRect.x + absRect.width));
const int16_t y1 = std::min(int16_t(source.y + source.height), int16_t(absRect.y + absRect.height));
const Rect blitRect(x0 - source.x, y0 - source.y, x1 - x0, y1 - y0);
```

This means Experiment C as captured here is a **flawed test** of blitCopy itself — what we tested is "blitCopy with screen-absolute blitRect" which is a misuse. A corrected re-run (Experiment C2) is needed before concluding blitCopy is unusable.

## What this tells us

Comparing experiment A (drawPartialBitmap, full src rect) with experiment C (blitCopy):

| Behavior | drawPartialBitmap (Exp A) | blitCopy (Exp C) |
|---|---|---|
| Cell (1,1) RED — negative X, negative Y | Not visible | Not visible (small artifact band only) |
| Cell (2,1) GREEN — positive X, negative Y | Stretched full top width | Same — stretched full top width |
| Cell (1,2) BLUE — negative X, positive Y | Not visible | Not visible |
| Cell (2,2) YELLOW — positive X, positive Y | Stretched full bottom width | **Gone — not visible** |

`blitCopy` is no improvement over `drawPartialBitmap` — in fact it makes things slightly worse: YELLOW (the only cell that was rendering at the right *vertical* position in Experiment A) now fails to render entirely. GREEN still stretches full-width across the top half.

The pattern with blitCopy is consistent with a hypothesis that the engine effectively does:

```
for each pixel (x, y) in blitRect:
    framebuffer[x, y] = sourceData[x, y]   // ignoring source.x/y entirely
```

That is, `source.x/y` is silently ignored and `blitRect` is interpreted as *both* the framebuffer destination region *and* the source buffer region. Under that model:

- Each cell's `blitCopy(..., source=(srcX, srcY, 256, 256), blitRect=absRect)` reads from buffer position `(absRect.x, absRect.y)` and writes to screen `(absRect.x, absRect.y)`.
- Drawing order: (1,1) RED → (2,1) GREEN → (1,2) BLUE → (2,2) YELLOW. Each writes the same `absRect` region, last write wins.
- For the first big dirty rect `(0, 0, 240, 72)` the last write should be YELLOW. But we see GREEN. So the model isn't quite right either.

Best partial explanation: `blitCopy` probably *does* honor `source.x/y` to *clip* against framebuffer bounds, but treats `source.y < 0` or `source.x < 0` as "skip entirely" rather than "advance source pointer by `-source.x/-source.y * stride`." That makes:

- (1,1) source.x=-136, source.y=-136 → skipped
- (2,1) source.x=120, source.y=-136 → skipped? But GREEN renders...

This doesn't fit either. Without disassembly I can't pin the exact rule. But the empirical conclusion stands: **`blitCopy` is *also* broken for our use case**.

## Status of the proposed workarounds from Experiment B

| Workaround | Status after Experiment C |
|---|---|
| `LCD::blitCopy` (the workaround Exp C tested) | **Rejected.** Same class of bug as `drawPartialBitmap`. |
| Server-side composition into a single 240×240 buffer | **Likely viable.** Since GREEN renders at full widget width when source.y is the first cell drawn after slate fill, drawing one bitmap covering the whole widget (no negative positioning, no partial src rect) probably works. This is what experiment D should test. |
| Static-bitmap comparison | **Still worth running** as Experiment E. If a static `BitmapDatabase` bitmap honors a partial-rect `drawPartialBitmap` call, the bug is specific to external dynamics. If not, the bug is in the LCD8bpp_ABGR2222 path generally. |

## Next experiment

**Experiment C2 (corrected) — blitCopy with proper source-relative blitRect.** Re-run the blitCopy patch with the intersection math above so `blitRect` is correctly source-relative. This is the test we *intended* to run; only then can we conclude whether blitCopy itself is broken or whether it works with correct arguments.

**Then Experiment D — server-side composition.** Allocate one 240×240 ABGR2222 buffer in TileCanvas (or Model). On every viewport change, manually copy the four cells' visible quadrants into that buffer at their correct offsets — pure CPU `memcpy` per row, no framework drawing. Register that single composed buffer with `dynamicBitmapCreateExternal`. In `TileCanvas::draw`, do exactly one `drawPartialBitmap` (or `blitCopy`) at (0, 0) with src = full buffer. This avoids the multi-position multi-bitmap question entirely and produces a known-correct draw call.

## Reproducing this experiment

Apply [`patch.diff`](patch.diff), build, launch. Screenshot recipe in [`Docs/Simulator-Linux.md`](../../../../../Simulator-Linux.md#capturing-a-simulator-screenshot).
