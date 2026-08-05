#ifndef TILEBLITPROBE_HPP
#define TILEBLITPROBE_HPP

#include <touchgfx/widgets/Widget.hpp>

#include <cstdint>

/**
 * Full-screen widget that blits one raw ABGR2222 tile at a cyclable position.
 * The draw path deliberately mirrors the RawTilesMap tutorial's TileCanvas:
 * LCD::blitCopy with a manually pre-clipped, source-relative blitRect, never
 * drawPartialBitmap — that call family has two confirmed defects
 * (negative-X drop, Y-overhang misplacement; the latter reproduced on
 * STM32U595 hardware). Position 1 re-creates the Y-overhang repro geometry
 * (60,220) so a correct render here is direct evidence blitCopy is safe on
 * the hardware draw path.
 */
class TileBlitProbe : public touchgfx::Widget
{
public:
    static constexpr int kPositions = 3;

    void setTile(const uint8_t* pixels, uint16_t dim)
    {
        mPixels = pixels;
        mDim    = dim;
        invalidate();
    }

    void cyclePosition()
    {
        mPos = static_cast<uint8_t>((mPos + 1) % kPositions);
        invalidate();
    }

    int position() const { return mPos; }

    virtual touchgfx::Rect getSolidRect() const
    {
        // draw() covers every pixel: slate background, then the tile.
        return touchgfx::Rect(0, 0, getWidth(), getHeight());
    }

    virtual void draw(const touchgfx::Rect& area) const;

private:
    const uint8_t* mPixels = nullptr;
    uint16_t       mDim    = 0;
    uint8_t        mPos    = 0;
};

#endif // TILEBLITPROBE_HPP
