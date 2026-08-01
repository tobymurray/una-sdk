#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <gui/common/FrontendApplication.hpp>

#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

#if defined(SIMULATOR)
    #include "touchgfx/canvas_widget_renderer/CanvasWidgetRenderer.hpp"
    #ifdef _WIN32
    #include "Windows.h"
    #endif
    #include <chrono>
    #include <cstdlib>
    #include <ctime>

namespace {
// The simulator binary can be launched from several working directories
// (build/bin/, simulator/gcc/, TouchGFX-GUI/, …); try each candidate path
// until one resolves. RAWTILES_PATH env var wins if set.
const char* kPackCandidates[] = {
    "../Resources/stanley.rawtiles",
    "../../Resources/stanley.rawtiles",
    "../../../../Resources/stanley.rawtiles",
    "../../../../../Resources/stanley.rawtiles", // launched from build/bin/
    "Resources/stanley.rawtiles",
    "Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles",
};

} // namespace
#endif

Model::Model()
    : modelListener(0)
    , mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(this);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(this);

#if defined(SIMULATOR)
    LOG_INFO("Application is running through simulator! \n");

    std::string fileStoreDir = SDK::Simulator::KernelHolder::Get().getFsPath();
    LOG_INFO("Path to files created by app:\n"
        "       [%s]\n", fileStoreDir.c_str());

    LOG_INFO("\n"
        "       Keys:                       \n"
        "       ----------------------------\n"
        "       1   L1,                     \n"
        "       2   L2,                     \n"
        "       3   R1,                     \n"
        "       4   R2,                     \n"
        "       z   L1+R2                   \n"
    );

    // Open the rawtiles pack. RAWTILES_PATH env var overrides; otherwise walk
    // the candidate list until one resolves.
    SDK::RawTiles::OpenResult openResult = SDK::RawTiles::OpenResult::FileNotFound;
    if (const char* envPath = std::getenv("RAWTILES_PATH")) {
        openResult = mTiles.openFromFile(envPath);
        LOG_INFO("rawtiles: opening %s -> %s\n",
                 envPath, SDK::RawTiles::Container::describeResult(openResult));
    } else {
        for (const char* candidate : kPackCandidates) {
            openResult = mTiles.openFromFile(candidate);
            if (openResult == SDK::RawTiles::OpenResult::Ok) {
                LOG_INFO("rawtiles: opened %s\n", candidate);
                break;
            }
        }
        if (openResult != SDK::RawTiles::OpenResult::Ok) {
            LOG_INFO("rawtiles: pack not found in any candidate path; set RAWTILES_PATH to override\n");
        }
    }

    if (mTiles.isOpen()) {
        const auto& h = mTiles.header();
        LOG_INFO("rawtiles header:\n"
                 "       tile_dim_px      %u\n"
                 "       zoom_range       [%u, %u]\n"
                 "       tile_count       %u\n"
                 "       bbox (deg)       lon [%.6f, %.6f] lat [%.6f, %.6f]\n",
                 h.tileDimPx, h.zoomMin, h.zoomMax, h.tileCount,
                 h.bboxMinLonUDeg / 1e6, h.bboxMaxLonUDeg / 1e6,
                 h.bboxMinLatUDeg / 1e6, h.bboxMaxLatUDeg / 1e6);

        for (uint32_t i = 0; i < h.tileCount; ++i) {
            auto entry = mTiles.getTileByIndex(i);
            LOG_DEBUG("       tile[%u]  z=%u x=%u y=%u  %u B\n",
                      i, entry.z, entry.x, entry.y, entry.tile.length);
        }

        // Pick the centre tile for the 3×3 viewport: the median tile at the
        // pack's highest zoom level. ABGR2222 in the pack matches TouchGFX's
        // Bitmap::ABGR2222 byte-for-byte, so tile bytes are referenced in-place
        // via dynamicBitmapCreateExternal — no pixel copy, no large BSS pool.
        //
        // Pool sizing: only DynamicBitmapData metadata is stored in the cache
        // pool (~16 bytes per slot). 4 KB is generous headroom for 9 entries.
        mViewport.tileDimPx = h.tileDimPx;

        // Find median z = zoom_max entry to use as centre. Falls back to the
        // very first index entry if zoom_max has no tiles (shouldn't happen
        // for a well-formed pack but the spec allows it).
        uint8_t  cz = h.zoomMax;
        uint32_t cx = 0;
        uint32_t cy = 0;
        bool     centreFound = false;
        if (mTiles.tileCountAtZoom(h.zoomMax) > 0) {
            // Pick the first index entry at zoom_max as the seam centre. For a
            // tile-rectangular pack that's (cx_min, cy_min), so the four
            // visible viewport quadrants extend right + down into existing
            // pack tiles: (cx, cy), (cx+1, cy), (cx, cy+1), (cx+1, cy+1) all
            // fall inside the pack's bbox by construction.
            //
            // Earlier code used the median index, which for an even number of
            // tile-columns selected (cx_max, cy_min) and pushed the right two
            // quadrants outside the pack.
            for (uint32_t i = 0; i < h.tileCount; ++i) {
                auto entry = mTiles.getTileByIndex(i);
                if (entry.z == h.zoomMax) {
                    cz = entry.z;
                    cx = entry.x;
                    cy = entry.y;
                    centreFound = true;
                    break;
                }
            }
        } else {
            auto first = mTiles.getTileByIndex(0);
            cz = first.z;
            cx = first.x;
            cy = first.y;
            centreFound = first.tile.valid();
        }

        LOG_INFO("rawtiles: viewport centre = z=%u x=%u y=%u\n", cz, cx, cy);

        if (centreFound) {
            // Pre-pass: count tiles that actually exist before calling setCache.
            // Passing numberOfDynamicBitmaps > bitmaps ever created leaves
            // uninitialized metadata slots that corrupt the rendering pipeline.
            int validCount = 0;
            for (int row = 0; row < TileViewport::kGrid; ++row) {
                for (int col = 0; col < TileViewport::kGrid; ++col) {
                    if (mTiles.getTile(cz, cx + uint32_t(col - 1),
                                           cy + uint32_t(row - 1)).valid()) {
                        ++validCount;
                    }
                }
            }
            LOG_INFO("rawtiles: %d/9 viewport tiles found in pack\n", validCount);

            static constexpr uint32_t kCachePoolBytes = 4u * 1024u;
            static uint16_t           sBitmapCachePool[kCachePoolBytes / sizeof(uint16_t)];
            touchgfx::Bitmap::setCache(sBitmapCachePool, kCachePoolBytes, validCount);

            int filled = 0;
            for (int row = 0; row < TileViewport::kGrid; ++row) {
                for (int col = 0; col < TileViewport::kGrid; ++col) {
                    const int      slot = row * TileViewport::kGrid + col;
                    const uint32_t tx   = cx + uint32_t(col - 1);
                    const uint32_t ty   = cy + uint32_t(row - 1);
                    auto tile = mTiles.getTile(cz, tx, ty);
                    if (!tile.valid()) {
                        continue;
                    }
                    touchgfx::BitmapId id = touchgfx::Bitmap::dynamicBitmapCreateExternal(
                            mViewport.tileDimPx, mViewport.tileDimPx,
                            tile.data, touchgfx::Bitmap::ABGR2222);
                    if (id == touchgfx::BITMAP_INVALID) {
                        LOG_INFO("rawtiles: dynamicBitmapCreateExternal failed for cell (%d,%d)\n",
                                 col, row);
                        continue;
                    }
                    mViewport.ids[slot] = id;
                    ++filled;
                }
            }
            LOG_INFO("rawtiles: viewport filled %d/9 cells\n", filled);
        }
    }
#endif
}

FrontendApplication& Model::application()
{
    return *static_cast<FrontendApplication*>(touchgfx::Application::getInstance());
}

void Model::tick()
{
    //LOG_DEBUG("called\n");

    if (mInvalidate) {
        mInvalidate = false;
        application().invalidate();
    }
}

void Model::exitApp()
{
    LOG_INFO("Manually exiting the application\n");
    // Cleanup recourses

    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(nullptr);

    mKernel.sys.exit(); // No return for real app

    // !!! For TouchGFX Simulator !!!
    // This function only sets a flag.
    // The current TouchGFX loop will be completed, meaning that depending
    // on where this function was called, Model::tick(), Model::handleKeyEvent(),
    // as well as handleTickEvent() and handleKeyEvent() for the
    // current screen will be called.
}

// IUserApp implementation
void Model::onStart()
{
    LOG_INFO("called\n");
}

void Model::onResume()
{
    LOG_INFO("called\n");

    // Redraw screen
    mInvalidate = true;
}

void Model::onStop()
{
    LOG_INFO("called\n");
}

void Model::onSuspend()
{
    LOG_INFO("called\n");
}

// Events from Service

bool Model::customMessageHandler(SDK::MessageBase *msg)
{
    // Model::Model() registers itself as the custom-message handler before
    // FrontendHeap binds a presenter, so the Service thread can deliver
    // sensor messages while modelListener is still null. Drop those rather
    // than dereferencing — by the time the user is looking at the UI, the
    // presenter is bound and updates resume.
    if (modelListener == nullptr) {
        return true;
    }

    switch (msg->getType()) {
        case CustomMessage::HR_VALUES:  {
            auto* m = static_cast<CustomMessage::HRValues*>(msg);
            LOG_DEBUG("hr %.1f, tl %.1f\n", m->heartRate, m->trustLevel);
            modelListener->updateHR(m->heartRate, m->trustLevel);
        } break;

        case CustomMessage::LOCATION_VALUES:  {
            auto* m = static_cast<CustomMessage::LocationValues*>(msg);
            LOG_DEBUG("GPS: %.6f, %.6f, %.1f\n", m->latitude, m->longitude, m->altitude);
            modelListener->updateGPS(m->latitude, m->longitude, m->altitude);
        } break;

        case CustomMessage::ELEVATION_VALUES:  {
            auto* m = static_cast<CustomMessage::ElevationValues*>(msg);
            LOG_DEBUG("Elevation: %.1f m\n", m->elevation);
            modelListener->updateElevation(m->elevation);
        } break;

        case CustomMessage::ACCELEROMETER_VALUES:  {
            auto* m = static_cast<CustomMessage::AccelerometerValues*>(msg);
            LOG_DEBUG("Acc: %.2f, %.2f, %.2f\n", m->x, m->y, m->z);
            modelListener->updateAccelerometer(m->x, m->y, m->z);
        } break;

        case CustomMessage::STEP_COUNTER_VALUES:  {
            auto* m = static_cast<CustomMessage::StepCounterValues*>(msg);
            LOG_DEBUG("Steps: %u\n", m->steps);
            modelListener->updateStepCounter(m->steps);
        } break;

        case CustomMessage::FLOORS_VALUES:  {
            auto* m = static_cast<CustomMessage::FloorsValues*>(msg);
            LOG_DEBUG("Floors: %u\n", m->floors);
            modelListener->updateFloorCounter(m->floors);
        } break;

        case CustomMessage::COMPASS_VALUES:  {
            auto* m = static_cast<CustomMessage::CompassValues*>(msg);
            LOG_DEBUG("Compass: %.1f\n", m->heading);
            modelListener->updateCompass(m->heading);
        } break;

        case CustomMessage::STATS_VALUES:  {
            auto* m = static_cast<CustomMessage::StatsValues*>(msg);
            LOG_DEBUG("Stats: S%.1f%% G%.1f%% TX:%.0f msg/s (%.0f B/s) RX:%.0f msg/s (%.0f B/s)\n",
                      m->serviceCpuPct, m->guiCpuPct,
                      m->txMsgRate, m->txByteRate,
                      m->rxMsgRate, m->rxByteRate);
            modelListener->updateStats(m->serviceCpuPct, m->guiCpuPct,
                                       m->txMsgRate, m->rxMsgRate,
                                       m->txByteRate, m->rxByteRate);
        } break;

        case CustomMessage::RTC_VALUES:  {
            auto* m = static_cast<CustomMessage::RtcValues*>(msg);
            LOG_DEBUG("RTC: %u\n", m->time);
            modelListener->updateRTC(m->time);
        } break;

        case CustomMessage::BATTERY_VALUES:  {
            auto* m = static_cast<CustomMessage::BatteryValues*>(msg);
            LOG_DEBUG("Battery: %.1f%%\n", m->level);
            modelListener->updateBattery(m->level);
        } break;

        case CustomMessage::PRESSURE_VALUES:  {
            auto* m = static_cast<CustomMessage::PressureValues*>(msg);
            LOG_DEBUG("Pressure: %.1f hPa\n", m->pressure);
            modelListener->updatePressure(m->pressure);
        } break;

        default:
            break;
    }

    return true;
}
