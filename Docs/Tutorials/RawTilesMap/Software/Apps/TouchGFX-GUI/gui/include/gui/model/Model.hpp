#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/RawTiles/Container.hpp"

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

    /**
     * @brief Access the loaded rawtiles pack. @c isOpen() is false if the
     *        pack failed to open (path not found, CRC mismatch, etc.).
     */
    const SDK::RawTiles::Container& tiles() const { return mTiles; }

protected:
    ModelListener* modelListener;           ///< Pointer to model listener

    // Fields required for for GUI <-> Service communication
    const SDK::Kernel& mKernel;             ///< Reference to kernel interface

    bool mInvalidate = false;               ///< Request to redraw current screen

    SDK::RawTiles::Container mTiles;        ///< Pack opened from Resources/stanley.rawtiles at boot

    // IUserApp implementation
    virtual void onStart()   override;
    virtual void onResume()  override;
    virtual void onStop()    override;
    virtual void onSuspend() override;

    // ICustomMessageHandler implementation
    virtual bool customMessageHandler(SDK::MessageBase *msg) override;
};

#endif // MODEL_HPP
