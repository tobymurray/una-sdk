# RawTilesMap — cell (1,1) substitution doesn't render (investigation, 2026-05-16)

## TL;DR

When a single viewport cell's `dynamicBitmapCreateExternal` bitmap points at a buffer that no other cell shares, the renderer silently ignores it and that cell's widget region appears empty (the slate background or stale content shows through). When ≥2 cells share the same pixel pointer, the substitution becomes visible — but at unexpected widget positions (see screenshot 02). When all 4 cells share the same pointer, the substitution renders correctly everywhere (screenshot 03).

The framework's metadata is internally consistent: `dynamicBitmapGetAddress(id)` returns the exact pointer we passed at both registration time and draw time, and the bytes at that address remain the pattern bytes throughout. So the bug appears to be inside `LCD8bpp_ABGR2222::drawPartialBitmap` (or one of its callees) in the prebuilt `libtouchgfx.a` — not in our model or canvas code.

This bundle exists so the next session can pick up without re-running every experiment.

## What was attempted to render

A 3×3 viewport of 256×256 ABGR2222 tiles, with the four "visible" cells (1,1), (2,1), (1,2), (2,2) each contributing one quadrant to the 240×240 widget. Slot (1,1)'s bitmap was substituted at registration time with a synthetic test pattern (solid red, or later a multi-color marker). All four bitmaps were created via `touchgfx::Bitmap::dynamicBitmapCreateExternal(256, 256, pixelData, Bitmap::ABGR2222)`. The seam center is anchored so each cell's visible quadrant covers one 120×120 corner of the widget.

Expected: a bright 120×120 red square in the widget's top-left, with real OSM map tiles in the other three quadrants.

Observed: real OSM map tiles in all four quadrants (screenshot 01).

## Reproduction

```bash
# Build
UNA_SDK=$PWD make -C Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI -f simulator/gcc/Makefile -j$(nproc)

# Run — pack-not-found path was fixed in b7d8c4b, so RAWTILES_PATH is no longer
# required when launching from build/bin/. RAWTILES_PATH still wins if set.
DISPLAY=:0.0 ./Docs/Tutorials/RawTilesMap/Software/Apps/TouchGFX-GUI/build/bin/simulator.out
```

To reproduce the substitution, re-apply the single-slot diag from commit `1c94d8b` (or its successors). The bug appears immediately on first paint.

Screenshot capture recipe is in [`Docs/Simulator-Linux.md`](../../../../Simulator-Linux.md#capturing-a-simulator-screenshot) (added in `0df6ec1`).

## Confirmed end-to-end via diagnostics

| Stage | Verification |
|---|---|
| Registration | `Bitmap::dynamicBitmapCreateExternal` returns a unique `BitmapId` (16, 17, 18, 19) for each call. |
| Pointer retention | `dynamicBitmapGetAddress(id) == pixelData` for every cell. See [logs/03](logs/03-stored-equals-passed-end-to-end.log). |
| Byte integrity | First byte at `dynamicBitmapGetAddress(16)` is still `0xc3` (the red pattern byte) at draw time, after all other registrations. |
| Slot mapping | `mViewport.ids[4] == 16` after registration; `mIds[4] == 16` in `TileCanvas::draw` after `setViewport`. Row-major indexing is symmetric on both sides. See [logs/04](logs/04-draw-time-confirms-renderer-sees-pattern.log). |
| Draw call | `HAL::lcd().drawPartialBitmap(bmp, drawX=-136, drawY=-136, src=(0,0,256,256), alpha=255, useOptimized=true)` is invoked for cell (1,1) with the correct args. |
| Widget geometry | `getWidth()/getHeight() = 240×240`, `area=(0,0 240×72)` for the first dirty rect, full widget covered over 11 dirty rects. See [logs/06](logs/06-dirty-rect-cascade-11-calls.log). |

## The key observation

The number of cells that share `sDebugPattern` as their pixel pointer affects whether *any* of them render the pattern:

| Substituted cells | Cells sharing `sDebugPattern` | Pattern visible on screen? | Screenshot |
|---|---|---|---|
| (1,1) only | 1 | **No** — looks identical to no substitution at all | [01](screenshots/01-baseline-only-1-1-substituted-NO-RED.png) |
| (1,1) and (2,2) | 2 | **Partial** — widget bottom half is red, top half is tile content. Not the TL+BR diagonal expected. | [02](screenshots/02-diagonal-1-1-and-2-2-substituted-BOTTOM-HALF-RED.png) |
| All 4 visible cells | 4 | **Yes** — entire canvas renders as the pattern | [03](screenshots/03-all-4-substituted-ALL-RED.png) |

The N=2 result is particularly suspicious: substituting *only* (1,1) and (2,2) (TL and BR widget positions) produces red in BL and BR. The (2,2)→BR mapping is correct; the (1,1)→TL mapping is wrong and its content appears to leak to BL instead. **This is the strongest single clue and is not explained by any of the ruled-out hypotheses.**

## Hypotheses ruled out

Each row below documents a hypothesis tested and rejected, with the diff that was tried and the resulting screenshot. The diag patches were all reverted before commit; the descriptions below are sufficient to recreate them.

| Hypothesis | Test | Result | Evidence |
|---|---|---|---|
| The framework didn't retain the pixel pointer | Add per-cell log of `dynamicBitmapGetAddress(id)` at registration and at draw time, plus first byte read at that address | `stored == passed` for every cell; byte at address is `0xc3` at both time points | [logs/03](logs/03-stored-equals-passed-end-to-end.log), [logs/04](logs/04-draw-time-confirms-renderer-sees-pattern.log) |
| Slot indexing differs between Model and TileCanvas | Compare slot index calculations — both use `row * kGrid + col` row-major | Identical indexing on both sides; `mIds[4]` always == `mViewport.ids[4]` | [logs/04](logs/04-draw-time-confirms-renderer-sees-pattern.log) |
| `useOptimized=true` short-circuits on negative target coords | Switch to `useOptimized=false` in `TileCanvas::draw` | Identical result — no red | [screenshots/05](screenshots/05-useOptimized-false-STILL-NO-RED.png) |
| Negative target coords are silently dropped | Pre-clip src rect so cell (1,1) draws at positive target (0,0) with src=(136,136,120,120) | Cell (1,1) area renders weirdly (mostly slate) but still no pattern — *and* the other cells now have broken positions, indicating the math was wrong for them, but cell (1,1) is broken independently | [screenshots/08](screenshots/08-pre-clipped-positive-target-WEIRD-NO-PATTERN.png) |
| `sDebugPattern` in BSS has an address-range constraint the SDL2 backend rejects | Switch to `malloc(1024*1024)` so the buffer lives at `0x7fxx...` like the mmap'd tile data | Identical address range as working cells, still no red | [screenshots/04](screenshots/04-heap-allocated-pattern-STILL-NO-RED.png), [logs/07](logs/07-heap-allocated-pattern.log) |
| The first `dynamicBitmapCreateExternal` after `setCache` doesn't bind pixels | Register a sacrificial first dummy bitmap with `sDebugPattern`, then start the real loop (cell (1,1) gets ID 17 instead of 16); also bump `numberOfDynamicBitmaps` to 5 | Cell (1,1) still doesn't render | [screenshots/06](screenshots/06-sacrificial-first-bitmap-STILL-NO-RED.png) |
| Uniform-byte (all 0xc3) bitmaps hit a transparency early-out | Replace solid red with the multi-color synthetic pattern from commit `44ab360` (gray bg + edge stripes + corner dots) | No pattern visible at all, identical screen to single-cell red case | [screenshots/07](screenshots/07-multi-color-pattern-STILL-NO-PATTERN.png), [logs/08](logs/08-multi-color-pattern.log) |
| The widget's dirty rect excludes cell (1,1)'s visible region | Log every `draw(area)` call (cap 40); confirm the full widget gets covered | 11 dirty rects cover the entire 240×240 widget; `drawPartialBitmap` is invoked for cell (1,1) in every call | [logs/06](logs/06-dirty-rect-cascade-11-calls.log) |

## Hypotheses still open

In rough order of perceived likelihood:

1. **SDL2 LCD backend caches an `SDL_Surface` keyed by pixel-pointer-value**, and the cache entry is only promoted to "renderable" when reference count ≥ 2. Would explain N=1 invisible, N=4 visible cleanly. The N=2 partial result would need a secondary explanation (drawing order?).
2. **`LCD8bpp_ABGR2222::drawPartialBitmap` has a bug with mixed-source dynamic bitmaps in the same frame** — it might pick up a cached row stride or src address from a previous draw and apply it to the current one. The fact that cells (2,2)/(2,1)/(1,2) render their *tile* content correctly while (1,1)'s red gets lost suggests something about the order or the data shape.
3. **`setDisplayOrientation(ORIENTATION_LANDSCAPE)` on a square 240×240 display applies a transform** that the static `BitmapDatabase` is rotated to match (per the orientation-end-to-end commit `703d25c`) but external dynamic bitmaps are not. Would explain why the synthetic pattern visibly *rotated* in earlier commits (e.g. `bc431d9`) but vanishes entirely when only one cell carries it.
4. **The dynamic bitmap cache pool gets corrupted at a specific size or fill ratio.** `setCache(pool, 4096 bytes, validCount=4)` is what we use; the doc says ~16 bytes/slot metadata, so we're well under capacity, but if the pool layout interacts with pixel pointers in unintended ways we wouldn't see it from the public API.

## Suggested next investigation steps

1. **Disassemble `Bitmap::dynamicBitmapCreateExternal` and `LCD8bpp_ABGR2222::drawPartialBitmap` from `ThirdParty/touchgfx/lib/linux/libtouchgfx.a`** (`objdump -d` filtered by `_ZN8touchgfx6Bitmap27dynamicBitmapCreateExternal...` and the LCD8 ABGR2222 mangled name). The internal lookup path from `BitmapId` → pixel pointer is the missing piece; this is the only ground-truth source.
2. **Strace the simulator** with `strace -f -e mmap,read,write` and grep for accesses to the `sDebugPattern` address. If the framework never reads from the address at draw time, we have direct evidence the rendering path bypasses `dynamicBitmapGetAddress`.
3. **Try LCD24bpp / RGB888 instead of ABGR2222** for the dynamic bitmaps. If the bug disappears, it's in the ABGR2222 fast-path specifically.
4. **Compare the byte layout of a static ABGR2222 bitmap** (one of the `image_Button_*` files, which render correctly) with what we hand to `dynamicBitmapCreateExternal`. The static path may prepend or align in ways we miss.
5. **Reach upstream TouchGFX support** with the 4-byte-aligned guarantee and the reproducer below.

## Minimal upstream-ready reproducer

```cpp
static uint8_t sBuf[256 * 256];      // 4-byte aligned in BSS
static uint16_t sPool[2048];          // 4 KB cache pool
Bitmap::setCache(sPool, 4096, 1);

std::memset(sBuf, 0xC3, sizeof(sBuf));  // solid ABGR2222 red

BitmapId id = Bitmap::dynamicBitmapCreateExternal(
    256, 256, sBuf, Bitmap::ABGR2222);
// id != BITMAP_INVALID
// Bitmap::dynamicBitmapGetAddress(id) == sBuf
// *Bitmap::dynamicBitmapGetAddress(id) == 0xC3

// Inside a Widget::draw(Rect&) override:
Bitmap bmp(id);
HAL::lcd().drawPartialBitmap(bmp, 0, 0,
    Rect(0, 0, 256, 256), 255, false);
// Expected: 256×256 widget area becomes solid red.
// Observed (in our build): widget area shows whatever was there before;
// the substitution does not render.
```

Both `useOptimized=true` and `useOptimized=false` exhibit the same behavior in our build (Linux x86-64, `ThirdParty/touchgfx/lib/linux/libtouchgfx.a`).

## Environment

- **OS**: Linux x86-64 (Arch Linux, kernel 7.0.5-arch1-1)
- **Compiler**: GCC 15 (host)
- **TouchGFX**: prebuilt `libtouchgfx.a` from `ThirdParty/touchgfx/lib/linux/`
- **Simulator backend**: SDL2 (system `libsdl2`)
- **Display**: 240×240 square, `setDisplayOrientation(ORIENTATION_LANDSCAPE)`, `LCD8bpp_ABGR2222` with `enableTextureMapperABGR2222_NearestNeighbor()`
- **Branch state when captured**: `experiments` at `0df6ec1` (commits `b7d8c4b`, `0df6ec1` are the only non-diag changes from this session; the cell-render bug pre-dates them)
