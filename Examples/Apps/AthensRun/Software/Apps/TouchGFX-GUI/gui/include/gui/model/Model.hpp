#ifndef MODEL_HPP
#define MODEL_HPP

#include "touchgfx/UIEventListener.hpp"

#include <texts/TextKeysAndLanguages.hpp>
#include <images/BitmapDatabase.hpp>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include <SDK/Utils/Utils.hpp>
#include <SDK/GUI/Config.hpp>
#include <SDK/GUI/Color.hpp>
#include <SDK/GUI/Button.hpp>

#include "Commands.hpp"
#include <gui/map/MapMath.hpp>
#include <gui/map/TraceBuffer.hpp>
#include <gui/map/TileCache.hpp>
#include <SDK/RawTiles/Container.hpp>
#include "MapPackPaths.hpp"
#include "MapPackTrustMarker.hpp"
#include "MapPackVerifyLog.hpp"
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
// App::Display -- minimum valid values for on-screen display.
// Below these thresholds the widget shows "---" instead of a number.
// ---------------------------------------------------------------------------
namespace App::Display
{
constexpr float kMinDist = 0.0f;   ///< km or mi  -- negative = no data
constexpr float kMinPace = 30.0f;  ///< sec/km or sec/mi -- below any human running pace
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

    // -----------------------------------------------------------------
    // Live map (AthensRun). All map data lives here because TouchGFX
    // destroys screens on every transition; the Track screen's map face
    // only renders it. The tile cache's 256 KiB of slots are file-static
    // in Model.cpp (kept out of the FrontendHeap object).
    // -----------------------------------------------------------------
    struct MapState {
        SDK::RawTiles::Container   container;
        AthensRun::TraceBuffer     trace;
        int64_t                    centerX16 = 0;   // world px @ z16
        int64_t                    centerY16 = 0;
        uint8_t                    zoom      = 16;
        bool                       fix       = false;

        // One-shot: the *structural* open (skipCrcVerify=true) is a pure
        // function of the on-disk bytes; if it fails structurally (bad
        // magic, truncated, ...) it will fail exactly the same way on every
        // future tick, so it is not worth retrying.
        bool                       containerOpenTried = false;
        SDK::RawTiles::OpenResult  openResult = SDK::RawTiles::OpenResult::FileNotFound;
        const char*                resolvedPath = nullptr; // set on structural Ok

        // Sticky once true. Re-checked (cheap: one marker-file read) on
        // every ensureMapPack() tick while structurallyOk() && !trusted --
        // this is what lets "waiting on Service's background CRC pass"
        // resolve on its own instead of being a permanent dead end.
        bool                       trusted = false;

        // Sticky once true: the background pass definitively found this
        // exact pack corrupt (CRC mismatch). Distinct from !trusted so the
        // GUI can show an actual error instead of "verifying" forever.
        bool                       corrupt = false;

        bool structurallyOk() const { return openResult == SDK::RawTiles::OpenResult::Ok; }
        // Renderable == structurally valid AND CRC-trusted (via a cached
        // marker, or a background pass that already finished before this
        // boot).
        bool packOpen() const { return structurallyOk() && trusted; }
    };
    MapState& mapState() { return mMap; }
    AthensRun::TileCache& tileCache();
    void cycleMapZoom();

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

    void ensureMapPack();

    MapState         mMap {};
    MapPackVerifyLog mMapPackLog;

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
