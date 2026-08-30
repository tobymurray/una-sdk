# ST Community bug report: drawPartialBitmap Y-clipping

**Posted.** The report went to the ST Community as
[rawPartialBitmap draws Y-overhang rows at dirty-rect top instead of screen
Y](https://community.st.com/stm32-mcus-touchgfx-and-gui-33/rawpartialbitmap-draws-y-overhang-rows-at-dirty-rect-top-instead-of-screeny-abgr2222-external-bitmap-touchgfx-4-26-1-166187),
against TouchGFX 4.26.1. That thread, not this file, is where any ST reply
will appear.

What is kept here is the drafting record: the reasoning behind the wording,
and the notes on which forum board and which framing get a framework defect
read. The reproduction case is `bug/drawpartialbitmap-y-clipping-repro`.

## Product forum

**STM32 MCUs TouchGFX and GUI** is the right board. ST has no separate bug
tracker, so the community forum is where framework defects get raised.

Board: https://community.st.com/t5/stm32-mcus-touchgfx-and-gui/bd-p/mcu-touch-gfx-gui-forum

Add labels/tags like `TouchGFX`, `drawPartialBitmap`, `ABGR2222` if the post
form offers them.

## Title

Recommended:

> drawPartialBitmap draws Y-overhang rows at dirty-rect top instead of screenY (ABGR2222 external bitmap, TouchGFX 4.26.1)

Shorter alternatives:

- TouchGFX 4.26.1: vertical clipping defect in LCD8bpp_ABGR2222 drawPartialBitmap
- Bitmap duplicated vertically when it overhangs the dirty rect (drawPartialBitmap, ABGR2222, 4.26.1)

## Description (post body)

**Environment**

- TouchGFX 4.26.1 (Engine + prebuilt library)
- LCD config: 8bpp, `LCD8bpp_ABGR2222`, 240x240
- Reproduced on Windows simulator (MSVC), Linux simulator, and STM32U595 hardware. Same TouchGFX source across all three, so this is not platform-specific.

**Summary**

When an ABGR2222 dynamic external bitmap is drawn with `LCD::drawPartialBitmap`
and the source rect extends past the dirty (clip) rect on the Y axis, the
clipping path writes the overhanging rows starting at the dirty rect's top
(`dirtyRect.y`) instead of at the requested `screenY`. The result is a
duplicated copy of those rows at the wrong vertical position.

X-axis clipping is correct: `screenX` can be negative or off-screen with no
issue. The defect is specific to vertical overhang.

**Trigger conditions (all must hold)**

- Bitmap format `Bitmap::ABGR2222` (the other three 8bpp 2222 formats compile to identical code, so they are likely affected too)
- Bitmap registered with `Bitmap::dynamicBitmapCreateExternal`
- Drawn via `LCD::drawPartialBitmap`, or `LCD::blitCopy` with a full-bitmap blitRect
- Source rect extends past the dirty rect on Y: `screenY + height > dirtyRect.y + dirtyRect.height`, or `screenY < dirtyRect.y`
- `screenY != dirtyRect.y` (typically `screenY != 0`)

Non-trigger: a non-zero `screenY` on its own is fine when the whole bitmap fits
inside the dirty rect (verified at `screenY = 1`).

**Minimal reproduction**

A 50x50 ABGR2222 external bitmap drawn at (60, 220) on a 240x240 screen, so it
overhangs the bottom edge by 30 px.

```cpp
// ABGR2222: 1 byte/pixel, bits MSB->LSB are AABBGGRR.
// Opaque red = alpha 3, blue 0, green 0, red 3 = 0b11000011 = 0xC3.
static uint8_t  pattern[50 * 50];
static uint16_t pool[2048];

for (size_t i = 0; i < sizeof(pattern); ++i)
    pattern[i] = 0xC3;

Bitmap::setCache(pool, sizeof(pool), 1);
BitmapId id = Bitmap::dynamicBitmapCreateExternal(
    50, 50, pattern, Bitmap::ABGR2222);

// Inside a Widget::draw():
Bitmap bmp(id);
Rect src(0, 0, bmp.getWidth(), bmp.getHeight());
HAL::lcd().drawPartialBitmap(bmp, 60, 220, src, 255, true);
```

**Expected:** a single red strip about 20 rows tall near the bottom of the screen.

**Observed:** that strip, plus a spurious second red strip near the top. The
bottom-overhang rows are written at `y = 0` (`dirtyRect.y`) instead of
`y = 220` (`screenY`).

**Impact**

Any widget that scrolls a bitmap into or out of view, anchors one near a screen
edge, or otherwise clips it vertically will show this artifact.

## Posting notes

- Attach the expected-vs-bug screenshots from the repro branch; they make the report land instantly.
- Keep the post self-contained rather than leaning on a link to the branch, which ST staff may not be able to open. A link at the bottom as "full repro branch" is a nice-to-have, not a substitute.
