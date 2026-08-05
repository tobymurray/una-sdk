#include <gui/main_screen/MainView.hpp>

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Color.hpp>

#include <cstdio>

MainView::MainView()
{

}

void MainView::setupScreen()
{
    MainViewBase::setupScreen();

    buttons.setL1(ButtonsSet::WHITE);   // cycle tile position
    buttons.setL2(ButtonsSet::WHITE);   // toggle stats overlay
    buttons.setR1(ButtonsSet::WHITE);   // re-run probe (warm timings)
    buttons.setR2(ButtonsSet::WHITE);   // exit

    // Tile layer covers the whole screen (and the tutorial's stock widgets).
    mTile.setPosition(0, 0, 240, 240);
    add(mTile);

    // Stats overlay: dimmed backdrop + one wildcard text area. The template
    // typography (TMP_REGULAR_18_L) already carries the full ASCII wildcard
    // glyph range, so no texts.xml change or asset regeneration is needed.
    mStatsBg.setPosition(0, 0, 240, 168);
    mStatsBg.setColor(touchgfx::Color::getColorFromRGB(0, 0, 0));
    mStatsBg.setAlpha(160);
    add(mStatsBg);

    mStats.setPosition(6, 4, 228, 160);
    mStats.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18_L));
    mStats.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    touchgfx::Unicode::snprintf(mStatsBuf, kStatsBufSize, "probe in ~1s...");
    mStats.setWildcard(mStatsBuf);
    add(mStats);
}

void MainView::tearDownScreen()
{
    MainViewBase::tearDownScreen();
}

void MainView::updateProbe()
{
    const RawTilesProbe::Result& r = presenter->probe().result();

    if (r.tileOk) {
        mTile.setTile(RawTilesProbe::tileData(), RawTilesProbe::kTileDim);
    }

    // Format with std::snprintf (touchgfx::Unicode::snprintf's %s wants
    // UnicodeChar*, and its hex support is minimal), convert once at the end.
    char text[kStatsBufSize];
    if (r.failStage[0] != '\0') {
        std::snprintf(
            text, sizeof(text),
            "run %d FAIL @ %s\n"
            "2:/maps %c  1:/maps %c\n"
            "0:/maps %c  2:/Apps %c\n"
            "scan %ums open %ums",
            r.runs, r.failStage,
            r.existHit[0] ? 'Y' : 'n', r.existHit[1] ? 'Y' : 'n',
            r.existHit[2] ? 'Y' : 'n', r.existHit[3] ? 'Y' : 'n',
            static_cast<unsigned>(r.existScanMs), static_cast<unsigned>(r.openMs));
    } else {
        std::snprintf(
            text, sizeof(text),
            "run %d  %s\n"
            "scan %ums open %ums\n"
            "z%u-%u n=%u  %u B\n"
            "tile z%u x%u y%u\n"
            "64K cold %ums warm %ums\n"
            "crc %08X %s",
            r.runs, RawTilesProbe::candidatePath(r.pathIdx),
            static_cast<unsigned>(r.existScanMs), static_cast<unsigned>(r.openMs),
            static_cast<unsigned>(r.zoomMin), static_cast<unsigned>(r.zoomMax),
            static_cast<unsigned>(r.tileCount), static_cast<unsigned>(r.fileSize),
            static_cast<unsigned>(r.tileZ), static_cast<unsigned>(r.tileX),
            static_cast<unsigned>(r.tileY),
            static_cast<unsigned>(r.tileReadMs), static_cast<unsigned>(r.rereadMs),
            static_cast<unsigned>(r.tileCrc),
            r.tileCrc == RawTilesProbe::kStanleyZmaxTileCrc ? "OK" : "??");
    }
    touchgfx::Unicode::strncpy(mStatsBuf, text, kStatsBufSize - 1);
    mStatsBuf[kStatsBufSize - 1] = 0;
    mStats.setWildcard(mStatsBuf);
    invalidate();
}

void MainView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        mTile.cyclePosition();
        invalidate();
    }

    if (key == Gui::Config::Button::L2) {
        mOverlayVisible = !mOverlayVisible;
        mStatsBg.setVisible(mOverlayVisible);
        mStats.setVisible(mOverlayVisible);
        invalidate();
    }

    if (key == Gui::Config::Button::R1) {
        presenter->rerunProbe();
    }

    if (key == Gui::Config::Button::R2) {
        presenter->exit();
    }
}
