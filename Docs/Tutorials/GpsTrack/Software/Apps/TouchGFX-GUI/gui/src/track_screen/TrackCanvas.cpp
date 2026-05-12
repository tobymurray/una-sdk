#include <gui/track_screen/TrackCanvas.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

using namespace touchgfx;

TrackCanvas::TrackCanvas()
    : mBuf(nullptr), mCount(0),
      mCurLat(0.0f), mCurLon(0.0f), mHasCurrent(false)
{
}

void TrackCanvas::setTrack(const Model::GpsPoint* buf, uint16_t count)
{
    mBuf   = buf;
    mCount = count;
}

void TrackCanvas::setCurrentPos(float lat, float lon)
{
    mCurLat     = lat;
    mCurLon     = lon;
    mHasCurrent = true;
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

    if (!mBuf) return;

    const colortype teal  = Color::getColorFromRGB(0, 128, 128);
    const colortype white = Color::getColorFromRGB(192, 192, 192);

    for (uint16_t i = 0; i < mCount; i++) {
        auto px = static_cast<int16_t>(kOriginX + (mBuf[i].lon - kCenterLon) * kLonToPixel + 0.5f);
        auto py = static_cast<int16_t>(kOriginY - (mBuf[i].lat - kCenterLat) * kLatToPixel + 0.5f);
        dot(px, py, 1, teal, area);
    }

    if (mHasCurrent) {
        auto px = static_cast<int16_t>(kOriginX + (mCurLon - kCenterLon) * kLonToPixel + 0.5f);
        auto py = static_cast<int16_t>(kOriginY - (mCurLat - kCenterLat) * kLatToPixel + 0.5f);
        dot(px, py, 3, white, area);
    }
}
