#ifndef TRACKCANVAS_HPP
#define TRACKCANVAS_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/hal/Types.hpp>
#include <gui/model/Model.hpp>

class TrackCanvas : public touchgfx::Widget
{
public:
    TrackCanvas();

    void setTrack(const Model::GpsPoint* buf, uint16_t count);
    void setCurrentPos(float lat, float lon);

    virtual void draw(const touchgfx::Rect& area) const override;
    virtual touchgfx::Rect getSolidRect() const override { return getRect(); }

private:
    static constexpr float   kCenterLat  = 49.2331f;
    static constexpr float   kCenterLon  = 28.4682f;
    static constexpr float   kLatToPixel = 38071.0f;
    static constexpr float   kLonToPixel = 24877.0f;
    static constexpr int16_t kOriginX    = 120;
    static constexpr int16_t kOriginY    = 103;

    const Model::GpsPoint* mBuf;
    uint16_t               mCount;
    float                  mCurLat;
    float                  mCurLon;
    bool                   mHasCurrent;

    void dot(int16_t px, int16_t py, int16_t r,
             touchgfx::colortype c, const touchgfx::Rect& clip) const;
};

#endif // TRACKCANVAS_HPP
