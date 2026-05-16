#include <gui/map_screen/TileCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

using namespace touchgfx;

TileCanvas::TileCanvas()
{
}

void TileCanvas::draw(const Rect& area) const
{
    // Step 3 placeholder: solid teal fills the widget bounds so we can confirm
    // map_screen is on the navigation path before wiring tile rendering.
    Rect bg = area;
    translateRectToAbsolute(bg);
    HAL::lcd().fillRect(bg, Color::getColorFromRGB(0, 96, 96), 255);
}
