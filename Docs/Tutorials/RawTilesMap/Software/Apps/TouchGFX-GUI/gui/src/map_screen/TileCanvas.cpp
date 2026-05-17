#include <gui/map_screen/TileCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

#include <algorithm>
#include <cstring>

using namespace touchgfx;

TileCanvas::TileCanvas()
{
    for (int i = 0; i < kCells; ++i) {
        mIds[i] = BITMAP_INVALID;
    }
}

void TileCanvas::setViewport(const BitmapId* ids, uint16_t tileDimPx)
{
    std::memcpy(mIds, ids, sizeof(mIds));
    mTileDimPx = tileDimPx;
}

void TileCanvas::draw(const Rect& area) const
{
    // Sentinel slate background — every pixel must be covered, and any cell
    // the pack doesn't have a tile for ends up showing this colour. That
    // makes the pack's bbox visible on screen instead of leaving a hole.
    Rect bg = area;
    translateRectToAbsolute(bg);
    HAL::lcd().fillRect(bg, Color::getColorFromRGB(24, 32, 40), 255);

    if (mTileDimPx == 0) {
        return;
    }

    // Widget-local → absolute translation. Constant across this draw call.
    Rect absRect = area;
    translateRectToAbsolute(absRect);
    const int16_t absDX = static_cast<int16_t>(absRect.x - area.x);
    const int16_t absDY = static_cast<int16_t>(absRect.y - area.y);

    // Anchor the canvas so the corner shared by the four centre cells
    // ((1,1), (2,1), (1,2), (2,2)) lands at the widget centre. With 256-px
    // tiles on a 240-px widget each of those four cells exposes one quadrant.
    const int16_t halfW = static_cast<int16_t>(getWidth()  / 2);
    const int16_t halfH = static_cast<int16_t>(getHeight() / 2);

    // Use LCD::blitCopy directly rather than drawPartialBitmap: the prebuilt
    // ThirdParty/touchgfx/lib/linux/libtouchgfx.a has a bug in
    // LCD8bpp_ABGR2222::drawPartialBitmap where negative target X drops the
    // draw silently and positive target X stretches the source across the
    // widget width. blitCopy with a source-relative blitRect honors the
    // bitmap position correctly. See Investigations/2026-05-16-cell-render-bug/
    // for the full investigation and Experiment C2 for the verification.
    for (int row = 0; row < kGrid; ++row) {
        for (int col = 0; col < kGrid; ++col) {
            const BitmapId id = mIds[row * kGrid + col];
            if (id == BITMAP_INVALID) {
                continue;
            }
            const int16_t cellLocalX = static_cast<int16_t>((col - 2) * mTileDimPx + halfW);
            const int16_t cellLocalY = static_cast<int16_t>((row - 2) * mTileDimPx + halfH);

            // `source` is the bitmap's absolute screen rect (origin may be
            // negative when the cell extends off-screen). `blitRect` is the
            // intersection of `source` with the dirty rect, translated to
            // coords relative to source.x/y.
            Bitmap         bmp(id);
            const uint8_t* pixels = Bitmap::dynamicBitmapGetAddress(id);
            const Rect     source(static_cast<int16_t>(absDX + cellLocalX),
                                  static_cast<int16_t>(absDY + cellLocalY),
                                  static_cast<int16_t>(bmp.getWidth()),
                                  static_cast<int16_t>(bmp.getHeight()));

            const int16_t ix0 = std::max(source.x, absRect.x);
            const int16_t iy0 = std::max(source.y, absRect.y);
            const int16_t ix1 = std::min(static_cast<int16_t>(source.x + source.width),
                                         static_cast<int16_t>(absRect.x + absRect.width));
            const int16_t iy1 = std::min(static_cast<int16_t>(source.y + source.height),
                                         static_cast<int16_t>(absRect.y + absRect.height));
            if (ix1 <= ix0 || iy1 <= iy0) {
                continue;
            }
            const Rect blitRect(static_cast<int16_t>(ix0 - source.x),
                                static_cast<int16_t>(iy0 - source.y),
                                static_cast<int16_t>(ix1 - ix0),
                                static_cast<int16_t>(iy1 - iy0));
            HAL::lcd().blitCopy(pixels, Bitmap::ABGR2222, source, blitRect, 255, false);
        }
    }
}
