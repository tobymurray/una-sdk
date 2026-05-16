#ifndef MAPPRESENTER_HPP
#define MAPPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>
#include <gui/model/Model.hpp>

using namespace touchgfx;

class MapView;

class MapPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MapPresenter(MapView& v);

    virtual void activate() override;
    virtual void deactivate() override;
    virtual ~MapPresenter() {}

    void exit() { model->exitApp(); }

private:
    MapPresenter();
    MapView& view;
};

#endif // MAPPRESENTER_HPP
