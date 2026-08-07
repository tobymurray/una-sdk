/**
 ******************************************************************************
 * @file    MapTileView.hpp
 * @brief   Full-screen map widget: rawtiles basemap mosaic + GPS trace.
 *
 * One custom Widget paints everything in a single draw() pass, in order:
 * background fill, up to a 2x2 mosaic of 256 px tiles via LCD::blitCopy
 * (the device-proven path -- drawPartialBitmap has two confirmed clipping
 * defects on this target), the breadcrumb polyline (Bresenham steps of
 * 2x2 fillRects: no canvas buffer, no painter, correct over the blit by
 * construction), and the position marker. North-up; the viewport is
 * centred on the last fix.
 *
 * All data is borrowed from the Model (pack Container, TileCache,
 * TraceBuffer) because TouchGFX destroys and re-creates screens on every
 * transition -- nothing heavy may live in the view.
 ******************************************************************************
 */

#ifndef ATHENSRUN_MAPTILEVIEW_HPP
#define ATHENSRUN_MAPTILEVIEW_HPP

#include <gui/map/MapMath.hpp>
#include <gui/map/TileCache.hpp>
#include <gui/map/TraceBuffer.hpp>
#include <SDK/RawTiles/Container.hpp>

#include <touchgfx/widgets/Widget.hpp>

class MapTileView : public touchgfx::Widget
{
public:
    void setSources(const SDK::RawTiles::Container* container,
                    AthensRun::TileCache* cache,
                    const AthensRun::TraceBuffer* trace)
    {
        mContainer = container;
        mCache     = cache;
        mTrace     = trace;
    }

    /// Centre (world px at the trace storage zoom, z16), display zoom and
    /// fix state for the next draw.
    void setViewport(int64_t centerX16, int64_t centerY16, uint8_t zoom, bool fix)
    {
        mCenterX16 = centerX16;
        mCenterY16 = centerY16;
        mZoom      = zoom;
        mFix       = fix;
        invalidate();
    }

    virtual touchgfx::Rect getSolidRect() const
    {
        return touchgfx::Rect(0, 0, getWidth(), getHeight());
    }

    virtual void draw(const touchgfx::Rect& area) const;

private:
    void drawTiles(const touchgfx::Rect& absArea,
                   int64_t viewOriginX, int64_t viewOriginY,
                   int16_t absDX, int16_t absDY) const;
    void drawTrace(const touchgfx::Rect& absArea,
                   int64_t viewOriginX, int64_t viewOriginY,
                   int16_t absDX, int16_t absDY) const;
    void drawMarker(const touchgfx::Rect& absArea,
                    int16_t absDX, int16_t absDY) const;
    static void dot(const touchgfx::Rect& clip, int16_t absX, int16_t absY,
                    int16_t size, uint8_t r, uint8_t g, uint8_t b);

    const SDK::RawTiles::Container* mContainer = nullptr;
    AthensRun::TileCache*           mCache     = nullptr;
    const AthensRun::TraceBuffer*   mTrace     = nullptr;
    int64_t                         mCenterX16 = 0;
    int64_t                         mCenterY16 = 0;
    uint8_t                         mZoom      = 16;
    bool                            mFix       = false;
};

#endif // ATHENSRUN_MAPTILEVIEW_HPP
