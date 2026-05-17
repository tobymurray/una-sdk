# Checking the drawPartialBitmap bug on your platform

The Linux simulator works around a TouchGFX bug where `LCD::drawPartialBitmap`
silently loses ABGR2222 dynamic bitmaps if you pass a negative target x or a
source rect smaller than the bitmap. The workaround sits in
`Libs/Header/SDK/GUI/DynamicBitmapDraw.hpp`; the header comment explains
what it does and why.

The workaround only activates under `SIMULATOR && __linux__`. Windows
simulator and embedded targets still hit `drawPartialBitmap` directly. If
you're on either of those, here's how to find out whether your platform
needs the same workaround.

## Steps

1. Build HelloWorld and run it.
2. You should see a yellow smiley face on a pale-sky background covering
   most of the widget, with the rightmost ~30 px showing the underlying
   HelloWorld content where the bitmap doesn't reach. On Linux that comes
   via the workaround; everywhere else it comes from the `#else` branch of
   the helper, which is a direct `drawPartialBitmap` call.
3. Edit `Docs/Tutorials/HelloWorld/Software/Apps/TouchGFX-GUI/gui/include/gui/main_screen/MainView.hpp`.
   In `NegativeAnchorRepro::draw`, replace the `SDK::GUI::drawDynamicBitmap`
   call with `drawPartialBitmap` straight from the LCD:

   ```cpp
   touchgfx::Bitmap bmp(mId);
   touchgfx::Rect src(0, 0, bmp.getWidth(), bmp.getHeight());
   touchgfx::HAL::lcd().drawPartialBitmap(bmp,
       static_cast<int16_t>(absArea.x - 50),
       absArea.y, src, 255, true);
   ```
4. Rebuild and run.

## What you should see

Still a smiley face → `drawPartialBitmap` on your platform handles negative
x correctly. You don't need the workaround; the `__linux__` gate is fine.

No smiley face (the widget shows the standard HelloWorld screen instead) →
your platform has the same bug. Add your platform's identifier to the `#if`
guard in `DynamicBitmapDraw.hpp` so it picks up the workaround. For Windows
simulator that's `defined(_WIN32) && defined(SIMULATOR)`; for embedded,
whatever your build defines.

Write the result up somewhere people will find it (PR, issue, comment in the
header) so the gate stays current with what's been tested.

## Removing the demo

The HelloWorld changes only exist to make the bug reproducible. Drop the
`NegativeAnchorRepro` widget and its `setupScreen` wiring when you're done.
