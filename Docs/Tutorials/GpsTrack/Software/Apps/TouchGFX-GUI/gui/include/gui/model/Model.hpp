#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/TrackMap/TrackMapBuilder.hpp"

#include "gui/common/GuiConfig.hpp"
#include "Commands.hpp"

#include <vector>
#include <memory>

class FrontendApplication;
class ModelListener;

class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    using GpsPoint = SDK::TrackMapBuilder::GpsPoint;
    static constexpr uint16_t kMaxTrackPoints = 500;

    Model();

    void bind(ModelListener *listener)
    {
        modelListener = listener;
    }

    FrontendApplication &application();
    void tick();

    /**
     * @brief Exits the application.
     * This method notifies the kernel that the application is exiting and performs
     * any necessary cleanup before termination.
     */
    void exitApp();

    const GpsPoint* getTrackBuf()   const { return mTrackBuf; }
    uint16_t        getTrackCount() const { return mTrackCount; }
    float           getCurLat()     const { return mCurLat; }
    float           getCurLon()     const { return mCurLon; }

protected:
    ModelListener* modelListener;           ///< Pointer to model listener

    // Fields required for for GUI <-> Service communication
    const SDK::Kernel& mKernel;             ///< Reference to kernel interface

    bool mInvalidate = false;               ///< Request to redraw current screen

    GpsPoint mTrackBuf[kMaxTrackPoints]{};
    uint16_t mTrackCount = 0;
    uint16_t mTrackHead  = 0;
    float    mCurLat     = 0.0f;
    float    mCurLon     = 0.0f;

    // IUserApp implementation
    virtual void onStart()   override;
    virtual void onResume()  override;
    virtual void onStop()    override;
    virtual void onSuspend() override;

    // ICustomMessageHandler implementation
    virtual bool customMessageHandler(SDK::MessageBase *msg) override;
};

#endif // MODEL_HPP
