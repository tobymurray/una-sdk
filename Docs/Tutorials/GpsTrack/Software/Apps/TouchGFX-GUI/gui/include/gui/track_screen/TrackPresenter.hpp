#ifndef TRACKPRESENTER_HPP
#define TRACKPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>
#include <gui/model/Model.hpp>

using namespace touchgfx;

class TrackView;

class TrackPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TrackPresenter(TrackView& v);

    virtual void activate() override;
    virtual void deactivate() override;
    virtual ~TrackPresenter() {}

    virtual void updateHR(float hr, float tl) override;
    virtual void updateTrack(const Model::GpsPoint* buf, uint16_t count,
                              float curLat, float curLon) override;

    void exit() { model->exitApp(); }

private:
    TrackPresenter();
    TrackView& view;
};

#endif // TRACKPRESENTER_HPP
