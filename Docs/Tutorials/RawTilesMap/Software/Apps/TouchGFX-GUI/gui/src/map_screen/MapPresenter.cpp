#include <gui/map_screen/MapView.hpp>
#include <gui/map_screen/MapPresenter.hpp>

MapPresenter::MapPresenter(MapView& v)
    : view(v)
{
}

void MapPresenter::activate()
{
    view.setViewport(model->viewport());
}

void MapPresenter::deactivate()
{
}
