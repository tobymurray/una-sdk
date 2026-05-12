#include <gui/track_screen/TrackCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

using namespace touchgfx;

TrackCanvas::TrackCanvas()
    : mHasCurrent(false)
{
}

void TrackCanvas::setTrack(const Model::GpsPoint* buf, uint16_t count,
                           float curLat, float curLon)
{
    mBuilder.reset();
    for (uint16_t i = 0; i < count; ++i) {
        mBuilder.addPoint(buf[i]);
    }
    mHasCurrent = (count > 0);
    if (mHasCurrent) {
        // Appending curPos last guarantees the head marker tracks Model's
        // recorded current position even after the ring buffer wraps.
        mBuilder.addPoint({curLat, curLon});
    }
    mScreen = mBuilder.build(kRadiusPx);
}

void TrackCanvas::dot(int16_t px, int16_t py, int16_t r,
                      colortype c, const Rect& clip) const
{
    Rect d(static_cast<int16_t>(px - r), static_cast<int16_t>(py - r),
           static_cast<int16_t>(2 * r + 1), static_cast<int16_t>(2 * r + 1));
    Rect clipped = d & clip;
    if (clipped.isEmpty()) return;
    translateRectToAbsolute(clipped);
    HAL::lcd().fillRect(clipped, c, 255);
}

void TrackCanvas::draw(const Rect& area) const
{
    Rect bg = area;
    translateRectToAbsolute(bg);
    HAL::lcd().fillRect(bg, Color::getColorFromRGB(0, 0, 0), 255);

    if (mScreen.points.empty()) return;

    const colortype teal  = Color::getColorFromRGB(0, 128, 128);
    const colortype white = Color::getColorFromRGB(192, 192, 192);

    const size_t n = mScreen.points.size();
    const size_t headIdx = (mHasCurrent && n > 0) ? n - 1 : n;

    for (size_t i = 0; i < headIdx; ++i) {
        dot(mScreen.points[i].x, mScreen.points[i].y, 1, teal, area);
    }
    if (headIdx < n) {
        dot(mScreen.points[headIdx].x, mScreen.points[headIdx].y, 3, white, area);
    }
}
