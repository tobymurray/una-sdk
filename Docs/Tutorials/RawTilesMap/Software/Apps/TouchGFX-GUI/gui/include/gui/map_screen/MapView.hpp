#ifndef MAPVIEW_HPP
#define MAPVIEW_HPP

#include <mvp/View.hpp>
#include <gui/map_screen/MapPresenter.hpp>
#include <gui/map_screen/TileCanvas.hpp>
#include <gui/containers/ButtonsSet.hpp>
#include <gui/common/FrontendApplication.hpp>

class MapView : public touchgfx::View<MapPresenter>
{
public:
    MapView();
    virtual ~MapView() {}

    virtual void setupScreen();
    virtual void tearDownScreen();

    void setTileBitmap(touchgfx::BitmapId id);

protected:
    virtual void handleKeyEvent(uint8_t key) override;

private:
    TileCanvas tileCanvas;
    ButtonsSet buttons;

    FrontendApplication& application()
    {
        return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
    }
};

#endif // MAPVIEW_HPP
