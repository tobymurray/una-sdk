#include <gui/map_screen/MapView.hpp>
#include <gui/common/GuiConfig.hpp>

using namespace touchgfx;

MapView::MapView()
{
    tileCanvas.setPosition(0, 0, 240, 240);
    add(tileCanvas);

    buttons.setXY(0, 47);
    add(buttons);
}

void MapView::setupScreen()
{
    buttons.initialize();
    buttons.setL1(ButtonsSet::WHITE);
    buttons.setL2(ButtonsSet::WHITE);
    buttons.setR1(ButtonsSet::NONE);
    buttons.setR2(ButtonsSet::AMBER);
}

void MapView::tearDownScreen()
{
}

void MapView::handleKeyEvent(uint8_t key)
{
    if (key == SDK::GUI::Button::L1 || key == SDK::GUI::Button::L2) {
        application().gotoMainScreenNoTransition();
    }
    if (key == SDK::GUI::Button::R2) {
        presenter->exit();
    }
}
