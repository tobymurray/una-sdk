/**
 ******************************************************************************
 * @file    ImuFusion.hpp
 * @date    11-July-2026
 * @author  Toby Murray
 * @brief   Simulated FUSION_RAW sensor: 6-axis IMU (accel+gyro, raw int16).
 *
 *          Samples come from an ImuFusionSource — either a CSV recording
 *          replayed in a loop, or a deterministic synthetic baseline with
 *          keyboard-injected racquet swings (see ImuFusionSource.hpp for the
 *          data model and CSV format).
 *
 *          Unlike the event-style IMU sensors, this driver produces a
 *          continuous high-rate stream (apps typically connect at 100 Hz
 *          with batching, e.g. period 10 ms / latency 100 ms).  The manager
 *          thread wakes per batch window, not per sample, so sensorRefresh()
 *          emits every sample whose nominal time has elapsed — with evenly
 *          spaced, drift-free timestamps — rather than one sample per wakeup.
 ******************************************************************************/

#ifndef __IMU_FUSION_HPP
#define __IMU_FUSION_HPP

#include "SDK/Simulator/Components/SensorDriver.hpp"
#include "SDK/Simulator/Components/Sensors/IMU/ImuFusionSource.hpp"

#include <atomic>
#include <cstdint>

namespace Sensor
{
    class ImuFusion : public Interface::ISensor,
                      public Sensor::ISensorDriverCtrl
    {
    public:
        ImuFusion();

        Sensor::Driver& getDriver();

        /**
         * @brief One-time setup, called from Instance::SensorLayer::init()
         *        before any app can connect.
         *
         * @param csvPath Recording to replay, or nullptr/empty for the
         *                synthetic baseline+swings mode.  A path that fails
         *                to load logs a warning and falls back to synthetic
         *                mode so the simulator stays usable.
         */
        void configure(const char* csvPath);

        /**
         * @brief Queue one synthetic swing (forehand/backhand alternating).
         *
         *        Thread-safe; called from the GUI key handler.  Ignored in
         *        CSV playback mode.
         */
        void injectSwing();

        //// ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

        //// ISensor
        void sensorRefresh() override;

    private:
        /** Fastest supported sampling period (1 kHz). */
        static constexpr float skMinPeriodMs = 1.0f;

        /**
         * Most samples emitted per refresh.  Normal batches are bounded by
         * the data-queue capacity (500); anything beyond this means the
         * process stalled, and we drop the backlog instead of flooding
         * listeners (mirrors a hardware FIFO overflow).
         */
        static constexpr uint32_t skMaxBurst = 2048;

        void     emitSample(uint64_t nominalUs);
        uint64_t elapsedUs() const;

        Sensor::Driver  mDriver;
        ImuFusionSource mSource;

        // Written by the GUI/app threads, consumed on the manager thread.
        std::atomic<uint32_t> mPendingSwings{0};
        std::atomic<bool>     mRunning{false};
        std::atomic<bool>     mResync{false};
        std::atomic<uint64_t> mRequestedPeriodUs{0};

        // Manager-thread emission state (mEpochUs set before registration).
        uint64_t mEpochUs  = 0; /**< Wall clock at sdcStart, us            */
        uint64_t mPeriodUs = 0; /**< Nominal sample spacing, us            */
        uint64_t mNextUs   = 0; /**< Next sample's nominal time (rel), us  */
    };

} /* namespace Sensor */

#endif /* __IMU_FUSION_HPP */
