#include <gui/track_screen/TrackView.hpp>
#include <gui/track_screen/TrackPresenter.hpp>

TrackPresenter::TrackPresenter(TrackView& v)
    : view(v)
{
}

void TrackPresenter::activate()
{
    uint16_t count = model->getTrackCount();
    view.updateTrack(model->getTrackBuf(), count,
                     model->getCurLat(), model->getCurLon());
}

void TrackPresenter::deactivate()
{
}

void TrackPresenter::updateHR(float hr, float tl)
{
    view.updateHR(hr, tl);
}

void TrackPresenter::updateTrack(const Model::GpsPoint* buf, uint16_t count,
                                  float curLat, float curLon)
{
    view.updateTrack(buf, count, curLat, curLon);
}
