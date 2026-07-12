/**
 ******************************************************************************
 * @file    ImuFusionSource.hpp
 * @date    11-July-2026
 * @author  Toby Murray
 * @brief   Sample source for the simulated FUSION_RAW (accel+gyro) sensor.
 *
 *          Produces 6-axis IMU samples (raw int16 LSB, matching the layout of
 *          SDK::SensorDataParser::FusionRaw) from one of two sources:
 *
 *          1. CSV playback — rows recorded from real hardware are replayed in
 *             a loop, sample-and-hold between rows.  Format, one row per
 *             sample, comma separated:
 *
 *                 t_ms,ax,ay,az,gx,gy,gz
 *
 *             where t_ms is a strictly increasing time in milliseconds
 *             (float, absolute or relative — only deltas are used) and the
 *             six axis values are raw signed 16-bit sensor units.  Blank
 *             lines and lines starting with '#' are ignored; one optional
 *             header line (recognized by a non-numeric first field) is
 *             tolerated.  Any other malformed content fails the load with a
 *             line-numbered error so bad fixtures are loud, not silently
 *             skipped.
 *
 *          2. Synthetic — a resting-wrist baseline (gravity on +Z) plus
 *             racquet-swing bursts injected via triggerSwing().  Successive
 *             swings alternate forehand/backhand, which differ only in the
 *             sign of the gyro waveform — enough to exercise swing counting,
 *             peak-rate measurement, and stroke-side classification in apps.
 *             Output is deterministic: no noise, no randomness.
 *
 *          The class is intentionally free of simulator/SDK dependencies and
 *          performs no I/O outside loadCsvFile(), so it can be unit-tested on
 *          the host (see Tests/Host/simulator/ImuFusionSource_test.cpp).
 *
 *          Raw-unit convention used by the synthetic defaults (documented,
 *          not enforced): accelerometer ±8 g full scale → 4096 LSB/g,
 *          gyroscope ±2000 dps full scale → 16.4 LSB/dps.  Match them to the
 *          target hardware with setSwingParams() if they ever diverge.
 *
 *          Thread safety: NOT thread-safe.  Drive it from a single thread
 *          (the Sensor::Manager refresh thread); see Sensor::ImuFusion for
 *          the cross-thread trigger handling.
 ******************************************************************************/

#ifndef __IMU_FUSION_SOURCE_HPP
#define __IMU_FUSION_SOURCE_HPP

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace Sensor
{
    class ImuFusionSource
    {
    public:
        /** One 6-axis sample in raw sensor units. */
        struct Sample {
            int16_t ax{};
            int16_t ay{};
            int16_t az{};
            int16_t gx{};
            int16_t gy{};
            int16_t gz{};
        };

        /** One recorded row: sample + offset from the recording start. */
        struct Row {
            uint64_t offsetUs{};
            Sample   sample{};
        };

        /** Stroke side of a synthetic swing. */
        enum class SwingType : uint8_t {
            FOREHAND = 0,
            BACKHAND = 1,
        };

        /**
         * Synthetic-mode tuning.  Amplitudes are raw LSB (see the unit
         * convention in the file header), duration is microseconds.
         */
        struct SwingParams {
            int16_t  accelRestZ = 4096;   /**< Resting gravity on +Z (1 g)   */
            int16_t  accelPeak  = 16384;  /**< Swing accel peak on X (4 g)   */
            int16_t  gyroPeakZ  = 19660;  /**< Main rotation peak (~1200 dps)*/
            int16_t  gyroPeakX  = 4915;   /**< Wrist-roll peak (~300 dps)    */
            uint32_t durationUs = 300000; /**< Swing duration (300 ms)       */
        };

        ImuFusionSource() = default;

        /**
         * @brief Parse CSV rows from a stream (see file header for format).
         *
         * @param in      Input stream positioned at the start of the data.
         * @param outRows Parsed rows with offsets relative to the first row.
         *                Only modified on success.
         * @param error   Human-readable failure reason with a line number.
         * @return true on success (at least one row), false otherwise.
         */
        static bool loadCsv(std::istream& in,
                            std::vector<Row>& outRows,
                            std::string& error);

        /**
         * @brief Convenience wrapper: open @p path and call loadCsv().
         */
        static bool loadCsvFile(const char* path,
                                std::vector<Row>& outRows,
                                std::string& error);

        /**
         * @brief Install playback rows; a non-empty set switches the source
         *        from synthetic to playback mode.  Resets playback state.
         */
        void setRows(std::vector<Row> rows);

        /** @brief true when in playback mode (rows installed). */
        bool hasPlayback() const noexcept { return !mRows.empty(); }

        /** @brief Replace synthetic tuning (synthetic mode only). */
        void setSwingParams(const SwingParams& params) noexcept;

        /**
         * @brief Request one synthetic swing.  Swings run back-to-back in
         *        trigger order; sides alternate forehand → backhand → …
         *        Ignored (returning the side it would have had) in playback
         *        mode, where the recording is the single source of truth.
         *
         * @return The stroke side this swing will play with.
         */
        SwingType triggerSwing() noexcept;

        /** @brief Total swings triggered since construction or reset(). */
        uint32_t swingsTriggered() const noexcept { return mSwingsTriggered; }

        /**
         * @brief Produce the sample for playback time @p tUs.
         *
         *        @p tUs is microseconds since the source was (re)started and
         *        must be non-decreasing between calls; reset() starts a new
         *        timeline.
         */
        Sample sampleAt(uint64_t tUs) noexcept;

        /**
         * @brief Return to the initial runtime state (playback cursor, swing
         *        queue, counters).  Installed rows and tuning are preserved.
         */
        void reset() noexcept;

    private:
        Sample playbackSample(uint64_t tUs) noexcept;
        Sample syntheticSample(uint64_t tUs) noexcept;

        // Playback (sample-and-hold over mRows, looped every mLoopUs).
        std::vector<Row> mRows;
        uint64_t         mLoopUs      = 0;
        std::size_t      mCursor      = 0;
        uint64_t         mPrevModUs   = 0;

        // Synthetic swing state.
        SwingParams mParams{};
        uint32_t    mSwingsTriggered = 0; /**< Total triggered              */
        uint32_t    mSwingsStarted   = 0; /**< Of those, started playing    */
        bool        mSwingActive     = false;
        uint64_t    mSwingStartUs    = 0;
    };

} /* namespace Sensor */

#endif /* __IMU_FUSION_SOURCE_HPP */
