#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include <SDK/Utils/Utils.hpp>
#include <SDK/Units/Units.hpp>
#include <SDK/GUI/Config.hpp>
#include <SDK/GUI/Color.hpp>
#include <SDK/GUI/Button.hpp>

#include "Commands.hpp"
#include "Settings.hpp"
#include "ActivitySummary.hpp"
#include "Track.hpp"
#include "AppMenu.hpp"


// ---------------------------------------------------------------------------
// App::Config -- application-level constants (timing, frame rate).
// Screens include this transitively via Presenter -> ModelListener -> Model.hpp.
// ---------------------------------------------------------------------------
namespace App::Config
{
constexpr uint32_t kFrameRate = SDK::GUI::Config::kFrameRate;

constexpr uint32_t kMenuAnimationSteps = 4;
constexpr uint32_t kScreenTimeoutSteps = SDK::Utils::secToTicks(30, kFrameRate);  // 30 s

// HR thresholds
constexpr uint8_t kHrThresholdsCount = CustomMessage::kHrThresholdsCount;
} // namespace App::Config

// ---------------------------------------------------------------------------
// App::Display -- how measurements are rendered.
//
// Precision and conversion come from SDK::Units; this app only says where its
// data stops being trustworthy. A value under a policy's floor is reported as
// !valid and the widget draws "---" rather than a number.
// ---------------------------------------------------------------------------
namespace App::Display
{
/** @brief Distance: SDK precision tiers, with negative treated as "no fix yet". */
constexpr SDK::Units::Precision kDistance =
    SDK::Units::withPlaceholderBelow(SDK::Units::Policy::kDistance, 0.0f);

/** @brief Pace floor in seconds per displayed unit -- below any human running pace. */
constexpr float kMinPace = SDK::Units::Policy::kMinPaceSeconds;

constexpr float kMinHR = 20.0f;    ///< bpm -- below physiological minimum
} // namespace App::Display


class FrontendApplication;
class ModelListener;

class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    Model();

    void bind(ModelListener* listener) { modelListener = listener; }

    // Controls
    FrontendApplication& application();
    App::MenuNav::Nav&   menu();
    void tick();
    void handleKeyEvent(uint8_t key);
    void invalidate();
    void resetIdleTimer();
    void exitApp();

    // Date/Time
    void getDate(uint8_t& month, uint8_t& day, uint8_t& weekday);
    void getTime(uint8_t& h, uint8_t& m, uint8_t& s);

    // Power
    uint8_t getBatteryLevel() const;

    // Settings
    bool isUnitsImperial() const;

    /**
     * @brief The user's unit preference as a formatter, for presenters to hand
     *        to their view.
     *
     * Prefer this over isUnitsImperial() anywhere a measurement is about to be
     * displayed: it keeps conversion, precision and unit label together.
     */
    SDK::Units::Formatter units() const
    {
        return SDK::Units::Formatter(isUnitsImperial() ? SDK::Units::System::Imperial
                                                       : SDK::Units::System::Metric);
    }

    bool is12HourFormat() const;
    const uint8_t* getHrThresholds() const;
    uint8_t        getHrThresholdsCount() const;
    const Settings& getSettings() const;
    void saveSettings(const Settings& sett);

    // GPS
    bool hasGpsFix() const;

    // Latest external-HR link status (SDK::Accessory::State); the kernel only
    // sends on change, so screens read this on activate to show the current icon.
    uint8_t getAccessoryState() const;

    // Hold-to-confirm: which action the shared TrackHoldConfirmation screen performs.
    enum class HoldConfirmMode { Finish, Discard };
    void setHoldConfirmMode(HoldConfirmMode mode);
    HoldConfirmMode getHoldConfirmMode() const;

    // Track
    void setPendingIntervalsMode(bool mode);
    bool isPendingIntervalsMode() const;
    const Track::IntervalsData& getPendingAlertIntervals() const;
    void trackStart(bool intervalsMode);
    void intervalsNextPhase();
    bool isTrackActive() const;
    void trackPause();
    void trackResume();
    bool isTrackPaused() const;
    const Track::Data& getTrackData() const;
    void saveLap();
    void saveTrack();
    void discardTrack();
    bool isTrackSummaryAvailable() const;
    const ActivitySummary& getTrackSummary() const;

private:
    // Fields required for GUI <-> Service communication
    ModelListener*           modelListener;
    const SDK::Kernel&       mKernel;

    // IGuiLifeCycleCallback
    void onStart()   override;
    void onResume()  override;
    void onSuspend() override;
    void onStop()    override;

    // ICustomMessageHandler
    bool customMessageHandler(SDK::MessageBase* message) override;

    void decIdleTimer();
    bool isAnyKeyPressed(uint8_t key) const;

    // State
    bool     mIsRunning  = false;
    bool     mInvalidate = false;
    uint32_t mIdleTimer  = 0;

    App::MenuNav::Nav mMenu {};
    std::tm           mTime {};

    // Settings (mirrored from Service)
    bool mUnitsImperial = false;
    bool mTimeFormat12h = false;
    uint8_t mHrThresholds[App::Config::kHrThresholdsCount] = {};
    uint8_t mHrThresholdsCount = App::Config::kHrThresholdsCount;
    Settings mSettings {};

    // Kernel state
    bool    mGpsFix         = false;
    uint8_t mBatteryLevel   = 0;
    uint8_t mAccessoryState = 0;   // SDK::Accessory::State (0 = UNAVAILABLE)

    // Track
    HoldConfirmMode        mHoldConfirmMode       = HoldConfirmMode::Discard;
    bool                   mPendingIntervalsMode  = false;
    Track::IntervalsData   mPendingAlertIntervals {};  ///< Snapshot from last INTERVALS_PHASE_ALERT
    Track::State           mTrackState            {};
    const ActivitySummary* mActivitySummary = nullptr;
    Track::Data            mTrackData             {};
};

#endif // MODEL_HPP
