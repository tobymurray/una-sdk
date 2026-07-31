#ifndef TRACKLAPVIEW_HPP
#define TRACKLAPVIEW_HPP

#include <gui_generated/tracklap_screen/TrackLapViewBase.hpp>
#include <gui/tracklap_screen/TrackLapPresenter.hpp>
#include <gui/containers/CountdownTimer.hpp>
#include <touchgfx/Callback.hpp>

class TrackLapView : public TrackLapViewBase
{
public:
    TrackLapView();
    virtual ~TrackLapView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setUnitsImperial(bool isImperial);
    void setLapNum(uint32_t n);

    /// @param metres  Lap distance in metres. Always metres.
    void setDistance(float metres);
    void setTimer(std::time_t sec);
    /// @param secondsPerMetre  Lap pace in s/m. Always s/m.
    void setPace(float secondsPerMetre);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    void onDismiss();

    /// The screen's single source of truth for the user's unit preference.
    SDK::Units::Formatter mUnits;

    CountdownTimer                      mDismissTimer;
    touchgfx::Callback<TrackLapView>    mDismissCb;
};

#endif // TRACKLAPVIEW_HPP
