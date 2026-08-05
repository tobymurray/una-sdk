#include <gui/main_screen/TileBlitProbe.hpp>

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

#include <algorithm>

using namespace touchgfx;

namespace {

// Tile top-left corner in widget-local coordinates per position. All three
// overhang the 240x240 screen so every draw exercises partial-blit clipping:
//  0: centred full-bleed — all four edges clipped, whole screen is tile
//  1: (60,220) — the drawPartialBitmap Y-overhang repro geometry
//  2: (-120,-120) — negative-X/-Y: the negative-X-drop defect's condition
const int16_t kPosX[TileBlitProbe::kPositions] = { -8,  60, -120 };
const int16_t kPosY[TileBlitProbe::kPositions] = { -8, 220, -120 };

} // namespace

void TileBlitProbe::draw(const Rect& area) const
{
    // Sentinel slate everywhere first, so "tile missing" and "tile clipped
    // away" look deliberate instead of leaving stale framebuffer content.
    Rect bg = area;
    translateRectToAbsolute(bg);
    HAL::lcd().fillRect(bg, Color::getColorFromRGB(24, 32, 40), 255);

    if (mPixels == nullptr || mDim == 0) {
        return;
    }

    Rect absRect = area;
    translateRectToAbsolute(absRect);
    const int16_t absDX = static_cast<int16_t>(absRect.x - area.x);
    const int16_t absDY = static_cast<int16_t>(absRect.y - area.y);

    // `source` is the tile's absolute screen rect (origin may be negative);
    // `blitRect` is its intersection with the dirty rect, translated to
    // source-relative coordinates. Same discipline as TileCanvas.
    const Rect source(static_cast<int16_t>(absDX + kPosX[mPos]),
                      static_cast<int16_t>(absDY + kPosY[mPos]),
                      static_cast<int16_t>(mDim),
                      static_cast<int16_t>(mDim));

    const int16_t ix0 = std::max(source.x, absRect.x);
    const int16_t iy0 = std::max(source.y, absRect.y);
    const int16_t ix1 = std::min(static_cast<int16_t>(source.x + source.width),
                                 static_cast<int16_t>(absRect.x + absRect.width));
    const int16_t iy1 = std::min(static_cast<int16_t>(source.y + source.height),
                                 static_cast<int16_t>(absRect.y + absRect.height));
    if (ix1 <= ix0 || iy1 <= iy0) {
        return;
    }
    const Rect blitRect(static_cast<int16_t>(ix0 - source.x),
                        static_cast<int16_t>(iy0 - source.y),
                        static_cast<int16_t>(ix1 - ix0),
                        static_cast<int16_t>(iy1 - iy0));
    HAL::lcd().blitCopy(mPixels, Bitmap::ABGR2222, source, blitRect, 255, false);
}
