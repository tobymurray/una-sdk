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

    for (int row = 0; row < kGrid; ++row) {
        for (int col = 0; col < kGrid; ++col) {
            const BitmapId id = mIds[row * kGrid + col];
            if (id == BITMAP_INVALID) {
                continue;
            }
            const int16_t cellLocalX = static_cast<int16_t>((col - 2) * mTileDimPx + halfW);
            const int16_t cellLocalY = static_cast<int16_t>((row - 2) * mTileDimPx + halfH);

            // Clip cell against the dirty rect (widget-local coords).
            // Cells that extend off-screen (negative cellLocalX/Y) must not be
            // passed to drawPartialBitmap with negative draw coordinates — the
            // LCD renderer does not clip and would write outside the framebuffer.
            Rect clip(cellLocalX, cellLocalY,
                      static_cast<int16_t>(mTileDimPx),
                      static_cast<int16_t>(mTileDimPx));
            if (!clip.intersect(area)) {
                continue;
            }
            clip &= area;

            // src: the portion of the bitmap that falls in the clipped region.
            const Rect src(static_cast<int16_t>(clip.x - cellLocalX),
                           static_cast<int16_t>(clip.y - cellLocalY),
                           clip.width, clip.height);

            const int16_t drawX = static_cast<int16_t>(absDX + clip.x);
            const int16_t drawY = static_cast<int16_t>(absDY + clip.y);
            HAL::lcd().drawPartialBitmap(Bitmap(id), drawX, drawY, src, 255, true);
        }
    }
}
