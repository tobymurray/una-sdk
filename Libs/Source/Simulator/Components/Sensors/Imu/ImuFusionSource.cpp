/**
 ******************************************************************************
 * @file    ImuFusionSource.cpp
 * @date    11-July-2026
 * @author  Toby Murray
 * @brief   Sample source for the simulated FUSION_RAW (accel+gyro) sensor.
 *
 ******************************************************************************/

#include "SDK/Simulator/Components/Sensors/IMU/ImuFusionSource.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <limits>

namespace Sensor
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    /** Fallback loop tail when the recording has a single row. */
    constexpr uint64_t kSingleRowGapUs = 10000;

    int16_t saturateI16(int32_t value) noexcept
    {
        if (value > std::numeric_limits<int16_t>::max()) {
            return std::numeric_limits<int16_t>::max();
        }
        if (value < std::numeric_limits<int16_t>::min()) {
            return std::numeric_limits<int16_t>::min();
        }
        return static_cast<int16_t>(value);
    }

    std::string trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        const std::size_t begin = s.find_first_not_of(ws);
        if (begin == std::string::npos) {
            return {};
        }
        const std::size_t end = s.find_last_not_of(ws);
        return s.substr(begin, end - begin + 1);
    }

    /** Parse one fully-consumed numeric token; false on any leftover text. */
    bool parseNumber(const std::string& token, double& out)
    {
        const std::string t = trim(token);
        if (t.empty()) {
            return false;
        }

        const char* begin = t.c_str();
        char*       end   = nullptr;
        const double v    = std::strtod(begin, &end);

        if (end != begin + t.size() || !std::isfinite(v)) {
            return false;
        }

        out = v;
        return true;
    }

    /**
     * Split a data row into 7 numeric fields: t_ms,ax,ay,az,gx,gy,gz.
     * On failure fills @p error with the reason (no line number).
     */
    bool parseRow(const std::string& line,
                  double& timeMs,
                  double (&values)[6],
                  std::string& error)
    {
        double      fields[7];
        std::size_t fieldIdx = 0;
        std::size_t tokenBegin = 0;

        while (true) {
            const std::size_t comma = line.find(',', tokenBegin);
            const std::string token =
                line.substr(tokenBegin,
                            comma == std::string::npos ? std::string::npos
                                                       : comma - tokenBegin);

            if (fieldIdx >= 7) {
                error = "expected 7 fields (t_ms,ax,ay,az,gx,gy,gz), got more";
                return false;
            }

            if (!parseNumber(token, fields[fieldIdx])) {
                error = "field " + std::to_string(fieldIdx + 1) +
                        " is not a number: '" + trim(token) + "'";
                return false;
            }

            ++fieldIdx;

            if (comma == std::string::npos) {
                break;
            }
            tokenBegin = comma + 1;
        }

        if (fieldIdx != 7) {
            error = "expected 7 fields (t_ms,ax,ay,az,gx,gy,gz), got " +
                    std::to_string(fieldIdx);
            return false;
        }

        timeMs = fields[0];
        for (std::size_t i = 0; i < 6; ++i) {
            values[i] = fields[i + 1];
        }

        return true;
    }

    bool axisToI16(double value, int16_t& out)
    {
        const double rounded = std::round(value);
        if (rounded < std::numeric_limits<int16_t>::min() ||
            rounded > std::numeric_limits<int16_t>::max()) {
            return false;
        }
        out = static_cast<int16_t>(rounded);
        return true;
    }

} /* namespace */

bool ImuFusionSource::loadCsv(std::istream& in,
                              std::vector<Row>& outRows,
                              std::string& error)
{
    std::vector<Row> rows;
    std::string      line;
    std::size_t      lineNo        = 0;
    bool             headerSkipped = false;
    double           firstMs       = 0.0;
    uint64_t         prevOffsetUs  = 0;

    while (std::getline(in, line)) {
        ++lineNo;

        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        double      timeMs = 0.0;
        double      values[6];
        std::string rowError;

        if (!parseRow(trimmed, timeMs, values, rowError)) {
            // Tolerate a single leading header line ("t_ms,ax,...").  Only a
            // non-numeric first field qualifies — a malformed data row (e.g.
            // right values, wrong field count) must stay a loud error.
            double firstField = 0.0;
            const bool numericStart = parseNumber(
                trimmed.substr(0, trimmed.find(',')), firstField);
            if (rows.empty() && !headerSkipped && !numericStart) {
                headerSkipped = true;
                continue;
            }
            error = "line " + std::to_string(lineNo) + ": " + rowError;
            return false;
        }

        Row row;
        int16_t* const axes[6] = {&row.sample.ax, &row.sample.ay,
                                  &row.sample.az, &row.sample.gx,
                                  &row.sample.gy, &row.sample.gz};
        for (std::size_t i = 0; i < 6; ++i) {
            if (!axisToI16(values[i], *axes[i])) {
                error = "line " + std::to_string(lineNo) + ": field " +
                        std::to_string(i + 2) + " out of int16 range";
                return false;
            }
        }

        if (rows.empty()) {
            firstMs = timeMs;
        }

        const double offsetMs = timeMs - firstMs;
        if (offsetMs < 0.0) {
            error = "line " + std::to_string(lineNo) +
                    ": time going backwards";
            return false;
        }

        row.offsetUs =
            static_cast<uint64_t>(std::llround(offsetMs * 1000.0));

        if (!rows.empty() && row.offsetUs <= prevOffsetUs) {
            error = "line " + std::to_string(lineNo) +
                    ": time not strictly increasing (at microsecond "
                    "resolution)";
            return false;
        }

        prevOffsetUs = row.offsetUs;
        rows.push_back(row);
    }

    if (rows.empty()) {
        error = "no data rows";
        return false;
    }

    outRows = std::move(rows);
    error.clear();
    return true;
}

bool ImuFusionSource::loadCsvFile(const char* path,
                                  std::vector<Row>& outRows,
                                  std::string& error)
{
    if (path == nullptr || path[0] == '\0') {
        error = "empty path";
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        error = std::string("cannot open '") + path + "'";
        return false;
    }

    return loadCsv(file, outRows, error);
}

void ImuFusionSource::setRows(std::vector<Row> rows)
{
    mRows = std::move(rows);

    if (!mRows.empty()) {
        // Normalize so playback starts at offset 0; loadCsv() already
        // guarantees strictly increasing offsets (asserted here for rows
        // constructed by hand).
        const uint64_t base = mRows.front().offsetUs;
        for (std::size_t i = 0; i < mRows.size(); ++i) {
            assert(i == 0 || mRows[i].offsetUs > mRows[i - 1].offsetUs);
            mRows[i].offsetUs -= base;
        }

        const uint64_t tailGapUs =
            (mRows.size() >= 2)
                ? mRows.back().offsetUs - mRows[mRows.size() - 2].offsetUs
                : kSingleRowGapUs;

        mLoopUs = mRows.back().offsetUs + tailGapUs;
    } else {
        mLoopUs = 0;
    }

    reset();
}

void ImuFusionSource::setSwingParams(const SwingParams& params) noexcept
{
    mParams = params;
}

ImuFusionSource::SwingType ImuFusionSource::triggerSwing() noexcept
{
    // Sides strictly alternate in trigger order, so the side of the swing
    // queued now is the parity of its 0-based ordinal.
    const SwingType side = (mSwingsTriggered % 2 == 0) ? SwingType::FOREHAND
                                                       : SwingType::BACKHAND;

    // In playback mode the recording is the single source of truth; report
    // the side but queue nothing.
    if (!hasPlayback()) {
        ++mSwingsTriggered;
    }

    return side;
}

ImuFusionSource::Sample ImuFusionSource::sampleAt(uint64_t tUs) noexcept
{
    return hasPlayback() ? playbackSample(tUs) : syntheticSample(tUs);
}

void ImuFusionSource::reset() noexcept
{
    mCursor          = 0;
    mPrevModUs       = 0;
    mSwingsTriggered = 0;
    mSwingsStarted   = 0;
    mSwingActive     = false;
    mSwingStartUs    = 0;
}

ImuFusionSource::Sample ImuFusionSource::playbackSample(uint64_t tUs) noexcept
{
    const uint64_t modUs = tUs % mLoopUs;

    if (modUs < mPrevModUs) {
        mCursor = 0; // wrapped to a new loop iteration
    }
    mPrevModUs = modUs;

    while (mCursor + 1 < mRows.size() &&
           mRows[mCursor + 1].offsetUs <= modUs) {
        ++mCursor;
    }

    return mRows[mCursor].sample;
}

ImuFusionSource::Sample ImuFusionSource::syntheticSample(uint64_t tUs) noexcept
{
    // Resting wrist: gravity on +Z, everything else quiet.
    Sample sample;
    sample.az = mParams.accelRestZ;

    if (!mSwingActive && mSwingsStarted < mSwingsTriggered) {
        mSwingActive  = true;
        mSwingStartUs = tUs;
    }

    if (!mSwingActive) {
        return sample;
    }

    const uint64_t elapsedUs = tUs - mSwingStartUs;
    if (elapsedUs >= mParams.durationUs) {
        // This swing is over; a queued one starts on the next sample.
        mSwingActive = false;
        ++mSwingsStarted;
        return sample;
    }

    // The swing being played has 0-based ordinal mSwingsStarted.
    const bool  forehand = (mSwingsStarted % 2 == 0);
    const double sign    = forehand ? 1.0 : -1.0;
    const double phase   = static_cast<double>(elapsedUs) /
                           static_cast<double>(mParams.durationUs);

    // Half-sine main rotation; S-shaped wrist roll and swing-through
    // acceleration (forward then braking).
    const double mainRot = std::sin(kPi * phase);
    const double sCurve  = std::sin(2.0 * kPi * phase);

    sample.gz = saturateI16(
        static_cast<int32_t>(std::lround(sign * mParams.gyroPeakZ * mainRot)));
    sample.gx = saturateI16(
        static_cast<int32_t>(std::lround(sign * mParams.gyroPeakX * sCurve)));
    sample.ax = saturateI16(
        static_cast<int32_t>(std::lround(mParams.accelPeak * sCurve)));

    return sample;
}

} /* namespace Sensor */
