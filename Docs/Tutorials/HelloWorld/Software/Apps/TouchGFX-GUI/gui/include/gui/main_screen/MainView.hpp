#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui_generated/main_screen/MainViewBase.hpp>
#include <gui/main_screen/MainPresenter.hpp>
#include <gui/main_screen/TileBlitProbe.hpp>

#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class MainView : public MainViewBase
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    /// Refreshes the tile blit and the stats overlay from the probe result.
    void updateProbe();

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    static constexpr uint16_t kStatsBufSize = 256;

    TileBlitProbe                     mTile;
    touchgfx::Box                     mStatsBg;
    touchgfx::TextAreaWithOneWildcard mStats;
    touchgfx::Unicode::UnicodeChar    mStatsBuf[kStatsBufSize];
    bool                              mOverlayVisible = true;
};

#endif // MAINVIEW_HPP
