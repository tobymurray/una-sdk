#ifndef TILECANVAS_HPP
#define TILECANVAS_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/hal/Types.hpp>

class TileCanvas : public touchgfx::Widget
{
public:
    TileCanvas();

    virtual void draw(const touchgfx::Rect& area) const override;
    virtual touchgfx::Rect getSolidRect() const override { return getRect(); }
};

#endif // TILECANVAS_HPP
