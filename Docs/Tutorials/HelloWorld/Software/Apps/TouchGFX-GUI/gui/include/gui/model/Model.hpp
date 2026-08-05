#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"

#include "gui/common/GuiConfig.hpp"
#include "gui/model/RawTilesProbe.hpp"

#include <vector>
#include <memory>

class FrontendApplication;
class ModelListener;

class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback
{
public:
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

    /// Re-runs the rawtiles device probe (R1) and notifies the listener.
    void rerunProbe();

    const RawTilesProbe& probe() const { return mProbe; }

protected:
    ModelListener* modelListener;           ///< Pointer to model listener

    // Fields required for for GUI <-> Service communication
    const SDK::Kernel& mKernel;             ///< Reference to kernel interface

    bool mInvalidate = false;               ///< Request to redraw current screen

    RawTilesProbe mProbe{mKernel};          ///< rawtiles device-feasibility probe
    uint32_t mTickCount = 0;                ///< frames since boot
    bool mProbeRan = false;                 ///< first probe fired

    // IUserApp implementation
    virtual void onStart()   override;
    virtual void onResume()  override;
    virtual void onStop()    override;
    virtual void onSuspend() override;
};

#endif // MODEL_HPP
