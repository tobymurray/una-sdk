#ifndef FRONTENDHEAP_HPP
#define FRONTENDHEAP_HPP

#include <gui_generated/common/FrontendHeapBase.hpp>
#include <gui/track_screen/TrackView.hpp>
#include <gui/track_screen/TrackPresenter.hpp>

class FrontendHeap : public FrontendHeapBase
{
public:
    /* List any user-defined view types here*/
    typedef touchgfx::meta::TypeList< TrackView,
                            touchgfx::meta::Nil
                            > UserDefinedViewTypes;

    /* List any user-defined presenter types here*/
    typedef touchgfx::meta::TypeList< TrackPresenter,
                            touchgfx::meta::Nil
                            > UserDefinedPresenterTypes;

    /* List any user-defined transition types here*/
    typedef touchgfx::meta::TypeList< touchgfx::meta::Nil, //Replace this with first user-defined type
                            touchgfx::meta::Nil  //List must always end with meta::Nil !
                            > UserDefinedTransitionTypes;



    /* Calculate largest view, both from generated and user-defined typelists */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedViewTypes >::type MaxUserViewType;

    typedef touchgfx::meta::TypeList< MaxGeneratedViewType,
	        touchgfx::meta::TypeList< MaxUserViewType,
                            touchgfx::meta::Nil
                            > > CombinedViewTypes;

    typedef touchgfx::meta::select_type_maxsize< CombinedViewTypes >::type MaxViewType;

    /* Calculate largest presenter, both from generated and user-defined typelists */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedPresenterTypes >::type MaxUserPresenterType;

    typedef touchgfx::meta::TypeList< MaxGeneratedPresenterType,
            touchgfx::meta::TypeList< MaxUserPresenterType,
                            touchgfx::meta::Nil
                            > > CombinedPresenterTypes;
    typedef touchgfx::meta::select_type_maxsize< CombinedPresenterTypes >::type MaxPresenterType;

    /* Calculate largest transition, both from generated and user-defined typelists */
    typedef touchgfx::meta::select_type_maxsize< UserDefinedTransitionTypes >::type MaxUserTransitionType;

    typedef touchgfx::meta::TypeList< MaxGeneratedTransitionType,
            touchgfx::meta::TypeList< MaxUserTransitionType,
                            touchgfx::meta::Nil
                            > > CombinedTransitionTypes;
    typedef touchgfx::meta::select_type_maxsize< CombinedTransitionTypes >::type MaxTransitionType;

    static FrontendHeap& getInstance()
    {
        static FrontendHeap instance;
        return instance;
    }

    touchgfx::Partition< CombinedPresenterTypes, 1 > presenters;
    touchgfx::Partition< CombinedViewTypes, 1 > views;
    touchgfx::Partition< CombinedTransitionTypes, 1 > transitions;
    Model model;
    FrontendApplication app;

    virtual void gotoStartScreen(FrontendApplication& app) override
    {
        app.gotoTrackScreenNoTransition();
    }

private:
    FrontendHeap() : FrontendHeapBase(presenters, views, transitions, app),
                     app(model, *this)
    {
        gotoStartScreen(app);
    }
};

#endif // FRONTENDHEAP_HPP
