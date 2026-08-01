#ifndef TRACKVIEW_HPP
#define TRACKVIEW_HPP

#include <mvp/View.hpp>
#include <gui/track_screen/TrackPresenter.hpp>
#include <gui/track_screen/TrackCanvas.hpp>
#include <gui/containers/ButtonsSet.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/model/Model.hpp>

class TrackView : public touchgfx::View<TrackPresenter>
{
public:
    TrackView();
    virtual ~TrackView() {}

    virtual void setupScreen();
    virtual void tearDownScreen();

    void updateHR(float hr, float tl);
    void updateTrack(const Model::GpsPoint* buf, uint16_t count, float curLat, float curLon);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    TrackCanvas                          trackCanvas;
    ButtonsSet                           buttons;
    touchgfx::TextAreaWithOneWildcard    text_hr;

    static const uint16_t TEXT_HR_SIZE = 32;
    touchgfx::Unicode::UnicodeChar text_hrBuffer[TEXT_HR_SIZE];

    FrontendApplication& application()
    {
        return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    }
};

#endif // TRACKVIEW_HPP
