#include <gui/map_screen/TileCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

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

    // Diagnostic shape: pass the full bitmap as src and rely on TouchGFX to
    // clip the off-screen portion. Mirrors step 5's working call site (which
    // had a -8 anchor and full src). If this composes the mosaic correctly,
    // the bug was in our explicit clip-rect math; if it crashes, the framework
    // doesn't clip far-negative anchors and we need a different primitive
    // (LCD::blitCopy).
    for (int row = 0; row < kGrid; ++row) {
        for (int col = 0; col < kGrid; ++col) {
            const BitmapId id = mIds[row * kGrid + col];
            if (id == BITMAP_INVALID) {
                continue;
            }
            // Skip cells that are entirely off-screen so we don't ask the
            // renderer about anchors like (-392, -392).
            const int16_t cellLocalX = static_cast<int16_t>((col - 2) * mTileDimPx + halfW);
            const int16_t cellLocalY = static_cast<int16_t>((row - 2) * mTileDimPx + halfH);
            if (cellLocalX + mTileDimPx <= 0 || cellLocalY + mTileDimPx <= 0
                    || cellLocalX >= getWidth() || cellLocalY >= getHeight()) {
                continue;
            }

            Bitmap     bmp(id);
            const Rect src(0, 0,
                           static_cast<int16_t>(bmp.getWidth()),
                           static_cast<int16_t>(bmp.getHeight()));
            const int16_t drawX = static_cast<int16_t>(absDX + cellLocalX);
            const int16_t drawY = static_cast<int16_t>(absDY + cellLocalY);
            HAL::lcd().drawPartialBitmap(bmp, drawX, drawY, src, 255, true);
        }
    }
}
