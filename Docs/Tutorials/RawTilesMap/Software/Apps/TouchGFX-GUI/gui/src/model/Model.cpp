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
    #include <cstdio>
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
    "Resources/stanley.rawtiles",
    "Docs/Tutorials/RawTilesMap/Resources/stanley.rawtiles",
};

// ABGR2222 byte encoding: A[7:6] B[5:4] G[3:2] R[1:0], each channel
// 0..3 maps to displayed 0/85/170/255. Alpha must be 3 (opaque) per spec.
constexpr uint8_t kABGR_Black   = 0xC0; // A=3, B=0, G=0, R=0
constexpr uint8_t kABGR_White   = 0xFF; // A=B=G=R=3
constexpr uint8_t kABGR_Red     = 0xC3; // A=3, R=3
constexpr uint8_t kABGR_Green   = 0xCC; // A=3, G=3
constexpr uint8_t kABGR_Blue    = 0xF0; // A=3, B=3
constexpr uint8_t kABGR_Yellow  = 0xCF; // A=3, G=3, R=3
constexpr uint8_t kABGR_Cyan    = 0xFC; // A=3, B=3, G=3
constexpr uint8_t kABGR_Magenta = 0xF3; // A=3, B=3, R=3
constexpr uint8_t kABGR_Gray    = 0xEA; // A=3, B=G=R=2

void debugFillRect(uint8_t* buf, uint16_t dim, int x, int y, int w, int h, uint8_t color)
{
    for (int j = y; j < y + h; ++j) {
        if (j < 0 || j >= dim) continue;
        for (int i = x; i < x + w; ++i) {
            if (i < 0 || i >= dim) continue;
            buf[j * dim + i] = color;
        }
    }
}

// Fill @p buf (dim×dim ABGR2222) with a self-describing pattern that exposes
// orientation and seam-alignment bugs at a glance. Interpreting the rendered
// mosaic with the seam-centred viewport from TileCanvas:
//
//   - 16-px edge stripes show which edge ends up where:
//       top  = RED, bottom = BLUE, left  = GREEN, right = YELLOW.
//     A correct 4-quadrant mosaic shows a RED+BLUE horizontal cross and
//     GREEN+YELLOW vertical cross meeting at the widget centre (each stripe
//     16 px wide on either side of the seam).
//
//   - 32-px corner dots show which bitmap corner ends up where:
//       TL = BLACK, TR = WHITE, BL = MAGENTA, BR = CYAN.
//     In the four visible quadrants the user expects: CYAN at widget top-left
//     (cell (1,1) shows its SE corner), MAGENTA at widget top-right,
//     WHITE at widget bottom-left, BLACK at widget bottom-right. Anything
//     else means rotation or transposition.
//
//   - Background is a uniform gray so seam alignment of bg-coloured regions
//     is visible too (tiles drift → gray bands offset across seams).
void debugFillPattern(uint8_t* buf, uint16_t dim)
{
    debugFillRect(buf, dim, 0, 0, dim, dim, kABGR_Gray);

    // Edge stripes.
    debugFillRect(buf, dim, 0,         0,         dim, 16,  kABGR_Red);
    debugFillRect(buf, dim, 0,         dim - 16,  dim, 16,  kABGR_Blue);
    debugFillRect(buf, dim, 0,         0,         16,  dim, kABGR_Green);
    debugFillRect(buf, dim, dim - 16,  0,         16,  dim, kABGR_Yellow);

    // Corner dots — drawn last so they win over edge stripes at the corners.
    debugFillRect(buf, dim, 0,         0,         32, 32, kABGR_Black);
    debugFillRect(buf, dim, dim - 32,  0,         32, 32, kABGR_White);
    debugFillRect(buf, dim, 0,         dim - 32,  32, 32, kABGR_Magenta);
    debugFillRect(buf, dim, dim - 32,  dim - 32,  32, 32, kABGR_Cyan);
}

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

        // Hex-dump the leading 32 bytes of tile[0] for byte-level sanity
        // checking against the host driver. These are the top-left 32 pixels
        // of the first index entry's bitmap, ABGR2222.
        if (h.tileCount > 0) {
            auto t0 = mTiles.getTileByIndex(0);
            char hex[3 * 32 + 1];
            int  pos = 0;
            for (int i = 0; i < 32 && i < int(t0.tile.length); ++i) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x ", t0.tile.data[i]);
            }
            LOG_INFO("rawtiles: tile[0] first 32 bytes: %s\n", hex);
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

        // Optional: replace real tile bytes with a self-describing test pattern
        // (see debugFillPattern docs above for what the colours mean). Enable
        // by exporting RAWTILES_DEBUG_PATTERN=1 before launching the simulator.
        const bool useDebugPattern = std::getenv("RAWTILES_DEBUG_PATTERN") != nullptr;
        static uint8_t sDebugPattern[1024 * 1024]; // generous; uses dim*dim bytes
        if (useDebugPattern) {
            debugFillPattern(sDebugPattern, h.tileDimPx);
            LOG_INFO("rawtiles: RAWTILES_DEBUG_PATTERN active — viewport cells "
                     "carry a synthetic test image instead of real tile bytes\n");
        }

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
                    const void* pixelData = useDebugPattern ? sDebugPattern : tile.data;
                    touchgfx::BitmapId id = touchgfx::Bitmap::dynamicBitmapCreateExternal(
                            mViewport.tileDimPx, mViewport.tileDimPx,
                            pixelData, touchgfx::Bitmap::ABGR2222);
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
