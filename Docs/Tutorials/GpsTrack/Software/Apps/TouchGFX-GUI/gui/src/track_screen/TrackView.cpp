#include <gui/track_screen/TrackView.hpp>
#include <gui/common/GuiConfig.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/Unicode.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

TrackView::TrackView()
{
    trackCanvas.setPosition(0, 0, 240, 240);
    add(trackCanvas);

    buttons.setXY(0, 47);
    add(buttons);

    text_hr.setPosition(2, 2, 120, 18);
    text_hr.setColor(Color::getColorFromRGB(192, 192, 192));
    text_hr.setLinespacing(0);
    Unicode::snprintf(text_hrBuffer, TEXT_HR_SIZE, "");
    text_hr.setWildcard(text_hrBuffer);
    text_hr.setTypedText(TypedText(T_TMP_REGULAR_9));
    add(text_hr);
}

void TrackView::setupScreen()
{
    buttons.initialize();
    buttons.setL1(ButtonsSet::WHITE);
    buttons.setL2(ButtonsSet::WHITE);
    buttons.setR1(ButtonsSet::NONE);
    buttons.setR2(ButtonsSet::AMBER);
}

void TrackView::tearDownScreen()
{
}

void TrackView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1 || key == Gui::Config::Button::L2) {
        application().gotoMainScreenNoTransition();
    }
    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}

void TrackView::updateHR(float hr, float /*tl*/)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "HR: %.0f BPM", hr);
    Unicode::strncpy(text_hrBuffer, buf, TEXT_HR_SIZE);
    text_hr.invalidate();
}

void TrackView::updateTrack(const Model::GpsPoint* buf, uint16_t count,
                             float curLat, float curLon)
{
    trackCanvas.setTrack(buf, count);
    if (count > 0) {
        trackCanvas.setCurrentPos(curLat, curLon);
    }
    trackCanvas.invalidate();
}
