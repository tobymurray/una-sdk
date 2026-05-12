#ifndef TRACKCANVAS_HPP
#define TRACKCANVAS_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/hal/Types.hpp>
#include <gui/model/Model.hpp>
#include "SDK/TrackMap/TrackMapBuilder.hpp"
#include "SDK/TrackMap/TrackMapScreen.hpp"

class TrackCanvas : public touchgfx::Widget
{
public:
    TrackCanvas();

    void setTrack(const Model::GpsPoint* buf, uint16_t count,
                  float curLat, float curLon);

    virtual void draw(const touchgfx::Rect& area) const override;
    virtual touchgfx::Rect getSolidRect() const override { return getRect(); }

private:
    // Map output points span [0, 2*kRadiusPx]; 120 fits a 240×240 canvas.
    static constexpr uint8_t kRadiusPx = 120;

    SDK::TrackMapBuilder mBuilder;
    SDK::TrackMapScreen  mScreen;
    bool                 mHasCurrent;

    void dot(int16_t px, int16_t py, int16_t r,
             touchgfx::colortype c, const touchgfx::Rect& clip) const;
};

#endif // TRACKCANVAS_HPP
