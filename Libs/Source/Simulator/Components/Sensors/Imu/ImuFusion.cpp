/**
 ******************************************************************************
 * @file    ImuFusion.cpp
 * @date    11-July-2026
 * @author  Toby Murray
 * @brief   Simulated FUSION_RAW sensor: 6-axis IMU (accel+gyro, raw int16).
 *
 ******************************************************************************/

#define LOG_MODULE_PRX      "IMU.Fusion"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

#include "SDK/Simulator/Components/Sensors/IMU/ImuFusion.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "SDK/Simulator/Kernel/Mock/System.hpp"

#include <cmath>

namespace Sensor
{

using FusionRaw = SDK::SensorDataParser::FusionRaw;

ImuFusion::ImuFusion()
    : mDriver(*this,
              SDK::Sensor::Type::FUSION_RAW,
              FusionRaw::getFieldsNumber(),
              *this)
    , mSource()
{
}

Sensor::Driver& ImuFusion::getDriver()
{
    return mDriver;
}

void ImuFusion::configure(const char* csvPath)
{
    if (csvPath == nullptr || csvPath[0] == '\0') {
        LOG_INFO("synthetic mode (no CSV configured)\n");
        return;
    }

    std::vector<ImuFusionSource::Row> rows;
    std::string                       error;

    if (!ImuFusionSource::loadCsvFile(csvPath, rows, error)) {
        LOG_WARNING("CSV '%s' rejected (%s); falling back to synthetic mode\n",
                    csvPath, error.c_str());
        return;
    }

    const uint64_t spanUs = rows.back().offsetUs;
    mSource.setRows(std::move(rows));

    LOG_INFO("playback mode: '%s', %u ms of samples (looped)\n",
             csvPath, static_cast<uint32_t>(spanUs / 1000));
}

void ImuFusion::injectSwing()
{
    if (mSource.hasPlayback()) {
        // Playback mode never changes after configure(), so this read does
        // not race with the manager thread.
        LOG_INFO("swing key ignored in CSV playback mode\n");
        return;
    }

    if (!mRunning.load(std::memory_order_acquire)) {
        LOG_INFO("swing key ignored: no app is connected to FUSION_RAW\n");
        return;
    }

    mPendingSwings.fetch_add(1, std::memory_order_acq_rel);
}

float ImuFusion::sdcStart(Sensor::Driver* driver, float period)
{
    (void)driver;

    // Called before the sensor is registered with the manager, so no
    // sensorRefresh() can run concurrently.
    mPeriodUs = static_cast<uint64_t>(std::llround(period * 1000.0f));
    mEpochUs  = elapsedUs();
    mNextUs   = mPeriodUs;
    mPendingSwings.store(0, std::memory_order_relaxed);
    mResync.store(false, std::memory_order_relaxed);
    mSource.reset();
    mRunning.store(true, std::memory_order_release);

    LOG_INFO("start: %u us period (%s)\n",
             static_cast<uint32_t>(mPeriodUs),
             mSource.hasPlayback() ? "playback" : "synthetic");

    return period;
}

void ImuFusion::sdcStop(Sensor::Driver* driver)
{
    (void)driver;

    // Called after the sensor is unregistered from the manager, so no
    // sensorRefresh() is in flight.
    mRunning.store(false, std::memory_order_release);

    LOG_INFO("stopped\n");
}

float ImuFusion::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    (void)driver;

    // May race with sensorRefresh(); hand the new period over atomically
    // and let the manager thread apply it.
    mRequestedPeriodUs.store(
        static_cast<uint64_t>(std::llround(period * 1000.0f)),
        std::memory_order_release);
    mResync.store(true, std::memory_order_release);

    LOG_INFO("period update: %u us\n",
             static_cast<uint32_t>(std::llround(period * 1000.0f)));

    return period;
}

float ImuFusion::sdcGetMinPeriod(Sensor::Driver* driver)
{
    (void)driver;

    return skMinPeriodMs;
}

const char* ImuFusion::sdcGetDescription(Sensor::Driver* driver)
{
    (void)driver;

    return "IMU fusion (accel+gyro)";
}

void ImuFusion::sensorRefresh()
{
    if (!mRunning.load(std::memory_order_acquire)) {
        return;
    }

    uint32_t pending = mPendingSwings.exchange(0, std::memory_order_acq_rel);
    while (pending-- > 0) {
        const ImuFusionSource::SwingType side = mSource.triggerSwing();
        LOG_INFO("synthetic %s swing queued\n",
                 side == ImuFusionSource::SwingType::FOREHAND ? "forehand"
                                                              : "backhand");
    }

    if (mResync.exchange(false, std::memory_order_acq_rel)) {
        mPeriodUs = mRequestedPeriodUs.load(std::memory_order_acquire);
        mNextUs   = (elapsedUs() - mEpochUs) + mPeriodUs;
    }

    if (mPeriodUs == 0) {
        return; // defensive: never spin on a zero period
    }

    const uint64_t nowRelUs = elapsedUs() - mEpochUs;

    uint32_t emitted = 0;
    while (mNextUs <= nowRelUs && emitted < skMaxBurst) {
        emitSample(mNextUs);
        mNextUs += mPeriodUs;
        ++emitted;
    }

    if (emitted == skMaxBurst && mNextUs <= nowRelUs) {
        // The process stalled for longer than the burst budget; behave like
        // a hardware FIFO overflow and drop the backlog.
        LOG_WARNING("stalled: dropping %u ms of samples\n",
                    static_cast<uint32_t>((nowRelUs - mNextUs) / 1000));
        mNextUs = nowRelUs + mPeriodUs;
    }
}

void ImuFusion::emitSample(uint64_t nominalUs)
{
    const ImuFusionSource::Sample s = mSource.sampleAt(nominalUs);

    auto& sample = mDriver.getDataSample();
    sample.setTimestampUs(mEpochUs + nominalUs);
    sample.i[FusionRaw::Field::ACCEL_X] = s.ax;
    sample.i[FusionRaw::Field::ACCEL_Y] = s.ay;
    sample.i[FusionRaw::Field::ACCEL_Z] = s.az;
    sample.i[FusionRaw::Field::GYRO_X]  = s.gx;
    sample.i[FusionRaw::Field::GYRO_Y]  = s.gy;
    sample.i[FusionRaw::Field::GYRO_Z]  = s.gz;
    mDriver.pushDataSample();
}

uint64_t ImuFusion::elapsedUs() const
{
    return static_cast<uint64_t>(SDK::Simulator::Mock::System::GetTimeMs()) *
           1000ULL;
}

} /* namespace Sensor */
