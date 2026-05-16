#include <gui/map_screen/TileCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

using namespace touchgfx;

TileCanvas::TileCanvas()
{
}

void TileCanvas::setBitmap(BitmapId id)
{
    mBitmapId = id;
}

void TileCanvas::draw(const Rect& area) const
{
    if (mBitmapId == BITMAP_INVALID) {
        // Boot-time placeholder while Model is still wiring up the dynamic
        // bitmap. Lets us tell "screen reached" from "draw call missed."
        Rect bg = area;
        translateRectToAbsolute(bg);
        HAL::lcd().fillRect(bg, Color::getColorFromRGB(0, 96, 96), 255);
        return;
    }

    Bitmap        bmp(mBitmapId);
    const int16_t bw = static_cast<int16_t>(bmp.getWidth());
    const int16_t bh = static_cast<int16_t>(bmp.getHeight());

    // Center the tile inside the widget. For a 256-px tile on a 240-px screen
    // we draw negative offsets — drawPartialBitmap clips against the widget
    // bounds for us via the supplied area rect.
    const int16_t offsetX = static_cast<int16_t>((getWidth()  - bw) / 2);
    const int16_t offsetY = static_cast<int16_t>((getHeight() - bh) / 2);

    Rect absRect = area;
    translateRectToAbsolute(absRect);
    const int16_t absX = static_cast<int16_t>(absRect.x - area.x + offsetX);
    const int16_t absY = static_cast<int16_t>(absRect.y - area.y + offsetY);

    // The bitmap source rect describes the portion of the bitmap to draw.
    Rect src(0, 0, bw, bh);
    HAL::lcd().drawPartialBitmap(bmp, absX, absY, src, 255, true);
}
