/**
 * Host unit tests for the simulator's ImuFusionSource — the sample generator
 * behind the simulated FUSION_RAW sensor.
 *
 * Guards the two behaviors apps depend on when developing IMU features
 * against the simulator:
 *  - CSV playback: strict, line-numbered parsing; sample-and-hold selection;
 *    seamless looping.
 *  - Synthetic swings: quiet baseline, back-to-back queued swings, strict
 *    forehand/backhand alternation expressed as the gyro sign, determinism.
 */

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "SDK/Simulator/Components/Sensors/IMU/ImuFusionSource.hpp"

using Sensor::ImuFusionSource;
using Row       = ImuFusionSource::Row;
using Sample    = ImuFusionSource::Sample;
using SwingType = ImuFusionSource::SwingType;

namespace {

constexpr uint64_t kMs = 1000; // microseconds per millisecond

std::vector<Row> mustLoad(const std::string& csv)
{
    std::istringstream in(csv);
    std::vector<Row>   rows;
    std::string        error;
    EXPECT_TRUE(ImuFusionSource::loadCsv(in, rows, error)) << error;
    return rows;
}

std::string mustFail(const std::string& csv)
{
    std::istringstream in(csv);
    std::vector<Row>   rows;
    std::string        error;
    EXPECT_FALSE(ImuFusionSource::loadCsv(in, rows, error));
    EXPECT_FALSE(error.empty());
    return error;
}

bool isBaseline(const Sample& s, int16_t restZ)
{
    return s.ax == 0 && s.ay == 0 && s.az == restZ && s.gx == 0 &&
           s.gy == 0 && s.gz == 0;
}

////////////////////////////////////////////////////////////////////////////
// CSV parsing
////////////////////////////////////////////////////////////////////////////

TEST(ImuFusionSourceCsv, ParsesRowsWithHeaderCommentsAndCrlf)
{
    const auto rows = mustLoad(
        "t_ms,ax,ay,az,gx,gy,gz\r\n"
        "# comment line\n"
        "\n"
        "100.0,1,2,3,4,5,6\r\n"
        "110.5,-1,-2,-3,-4,-5,-6\n"
        "  120 , 7 , 8 , 9 , 10 , 11 , 12 \n");

    ASSERT_EQ(rows.size(), 3u);

    // Offsets are relative to the first row.
    EXPECT_EQ(rows[0].offsetUs, 0u);
    EXPECT_EQ(rows[1].offsetUs, 10500u);
    EXPECT_EQ(rows[2].offsetUs, 20000u);

    EXPECT_EQ(rows[0].sample.ax, 1);
    EXPECT_EQ(rows[0].sample.gz, 6);
    EXPECT_EQ(rows[1].sample.ay, -2);
    EXPECT_EQ(rows[2].sample.gx, 10);
}

TEST(ImuFusionSourceCsv, AcceptsExtremeInt16Values)
{
    const auto rows = mustLoad("0,32767,-32768,0,0,0,0\n1,0,0,0,0,0,0\n");
    EXPECT_EQ(rows[0].sample.ax, 32767);
    EXPECT_EQ(rows[0].sample.ay, -32768);
}

TEST(ImuFusionSourceCsv, RejectsWrongFieldCount)
{
    EXPECT_NE(mustFail("0,1,2,3,4,5\n").find("line 1"), std::string::npos);
    EXPECT_NE(mustFail("0,1,2,3,4,5,6,7\n").find("line 1"),
              std::string::npos);
}

TEST(ImuFusionSourceCsv, RejectsNonNumericAfterHeader)
{
    // The single header allowance applies to the first row only.
    const auto error = mustFail(
        "t_ms,ax,ay,az,gx,gy,gz\n"
        "0,1,2,3,4,5,6\n"
        "oops,1,2,3,4,5,6\n");
    EXPECT_NE(error.find("line 3"), std::string::npos);
}

TEST(ImuFusionSourceCsv, RejectsOutOfRangeValues)
{
    const auto error = mustFail("0,32768,0,0,0,0,0\n");
    EXPECT_NE(error.find("int16"), std::string::npos);
}

TEST(ImuFusionSourceCsv, RejectsNonMonotonicTime)
{
    EXPECT_NE(mustFail("0,0,0,0,0,0,0\n10,0,0,0,0,0,0\n10,0,0,0,0,0,0\n")
                  .find("line 3"),
              std::string::npos);
    EXPECT_NE(mustFail("10,0,0,0,0,0,0\n5,0,0,0,0,0,0\n").find("line 2"),
              std::string::npos);
}

TEST(ImuFusionSourceCsv, RejectsEmptyInput)
{
    EXPECT_NE(mustFail("# only comments\n\n").find("no data rows"),
              std::string::npos);
}

TEST(ImuFusionSourceCsv, LoadCsvFileReportsMissingFile)
{
    std::vector<Row> rows;
    std::string      error;
    EXPECT_FALSE(ImuFusionSource::loadCsvFile("", rows, error));
    EXPECT_FALSE(ImuFusionSource::loadCsvFile(
        "/nonexistent/imu_fusion_source_test.csv", rows, error));
    EXPECT_NE(error.find("cannot open"), std::string::npos);
}

////////////////////////////////////////////////////////////////////////////
// Playback
////////////////////////////////////////////////////////////////////////////

TEST(ImuFusionSourcePlayback, SampleAndHoldBetweenRows)
{
    ImuFusionSource source;
    source.setRows(mustLoad(
        "0,10,0,0,0,0,0\n"
        "10,20,0,0,0,0,0\n"
        "20,30,0,0,0,0,0\n"));
    ASSERT_TRUE(source.hasPlayback());

    EXPECT_EQ(source.sampleAt(0).ax, 10);
    EXPECT_EQ(source.sampleAt(9 * kMs).ax, 10);   // hold until the next row
    EXPECT_EQ(source.sampleAt(10 * kMs).ax, 20);  // switch exactly on it
    EXPECT_EQ(source.sampleAt(19 * kMs).ax, 20);
    EXPECT_EQ(source.sampleAt(20 * kMs).ax, 30);
}

TEST(ImuFusionSourcePlayback, LoopsWithTrailingGap)
{
    ImuFusionSource source;
    source.setRows(mustLoad(
        "0,10,0,0,0,0,0\n"
        "10,20,0,0,0,0,0\n"
        "20,30,0,0,0,0,0\n"));

    // Loop period = last offset (20 ms) + trailing gap (10 ms) = 30 ms.
    EXPECT_EQ(source.sampleAt(29 * kMs).ax, 30);
    EXPECT_EQ(source.sampleAt(30 * kMs).ax, 10); // second iteration
    EXPECT_EQ(source.sampleAt(45 * kMs).ax, 20);
    EXPECT_EQ(source.sampleAt(65 * kMs).ax, 10); // third iteration, 5 ms in
    // A wrap that lands at a HIGHER loop offset than the previous call
    // (45 ms → 80 ms: offset 15 ms → 20 ms) must also resolve correctly.
    EXPECT_EQ(source.sampleAt(80 * kMs).ax, 30);
}

TEST(ImuFusionSourcePlayback, SingleRowHoldsForever)
{
    ImuFusionSource source;
    source.setRows(mustLoad("5,42,1,2,3,4,5\n"));

    EXPECT_EQ(source.sampleAt(0).ax, 42);
    EXPECT_EQ(source.sampleAt(123456789).ax, 42);
}

TEST(ImuFusionSourcePlayback, NormalizesNonZeroFirstOffset)
{
    ImuFusionSource source;
    source.setRows(mustLoad(
        "1000,10,0,0,0,0,0\n"
        "1010,20,0,0,0,0,0\n"));

    EXPECT_EQ(source.sampleAt(0).ax, 10);
    EXPECT_EQ(source.sampleAt(10 * kMs).ax, 20);
}

TEST(ImuFusionSourcePlayback, TriggerSwingIsIgnoredInPlaybackMode)
{
    ImuFusionSource source;
    source.setRows(mustLoad("0,10,0,0,0,0,0\n"));

    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
    EXPECT_EQ(source.swingsTriggered(), 0u);
    EXPECT_EQ(source.sampleAt(0).ax, 10); // still pure playback
}

////////////////////////////////////////////////////////////////////////////
// Synthetic mode
////////////////////////////////////////////////////////////////////////////

TEST(ImuFusionSourceSynthetic, QuietBaselineWithoutSwings)
{
    ImuFusionSource source;
    const ImuFusionSource::SwingParams params{};

    EXPECT_TRUE(isBaseline(source.sampleAt(0), params.accelRestZ));
    EXPECT_TRUE(
        isBaseline(source.sampleAt(60u * 1000 * kMs), params.accelRestZ));
}

TEST(ImuFusionSourceSynthetic, SwingPeaksThenReturnsToBaseline)
{
    ImuFusionSource source;
    const ImuFusionSource::SwingParams params{};

    EXPECT_TRUE(isBaseline(source.sampleAt(0), params.accelRestZ));

    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
    EXPECT_EQ(source.swingsTriggered(), 1u);

    // The swing starts at the time of the first sample after the trigger.
    const uint64_t startUs = 10 * kMs;
    const Sample   start   = source.sampleAt(startUs);
    EXPECT_EQ(start.gz, 0); // phase 0 of the half-sine
    EXPECT_EQ(start.az, params.accelRestZ);

    // Forehand: main rotation positive, at its half-sine peak mid-swing.
    const Sample mid = source.sampleAt(startUs + params.durationUs / 2);
    EXPECT_EQ(mid.gz, params.gyroPeakZ);
    EXPECT_EQ(mid.az, params.accelRestZ); // gravity retained throughout

    // Late in the swing the S-curve is in its braking (negative) phase.
    const Sample brake = source.sampleAt(startUs + params.durationUs * 3 / 4);
    EXPECT_LT(brake.ax, 0);

    // After the swing the wrist is quiet again.
    EXPECT_TRUE(isBaseline(source.sampleAt(startUs + params.durationUs),
                           params.accelRestZ));
    EXPECT_TRUE(isBaseline(source.sampleAt(startUs + 2 * params.durationUs),
                           params.accelRestZ));
}

TEST(ImuFusionSourceSynthetic, SwingSidesAlternateBySign)
{
    ImuFusionSource source;
    const ImuFusionSource::SwingParams params{};
    const uint64_t d = params.durationUs;

    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
    source.sampleAt(0); // forehand starts at t=0
    const Sample forehand = source.sampleAt(d / 2);

    source.sampleAt(d); // forehand ends
    EXPECT_EQ(source.triggerSwing(), SwingType::BACKHAND);
    source.sampleAt(2 * d); // backhand starts here
    const Sample backhand = source.sampleAt(2 * d + d / 2);

    EXPECT_GT(forehand.gz, 0);
    EXPECT_LT(backhand.gz, 0);
    EXPECT_EQ(forehand.gz, -backhand.gz); // mirrored waveform
}

TEST(ImuFusionSourceSynthetic, QueuedSwingsPlayBackToBack)
{
    ImuFusionSource source;
    const ImuFusionSource::SwingParams params{};
    const uint64_t stepUs = 10 * kMs; // 100 Hz sampling

    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
    EXPECT_EQ(source.triggerSwing(), SwingType::BACKHAND);
    EXPECT_EQ(source.swingsTriggered(), 2u);

    // Walk through both swings and record the sign of every active sample.
    bool sawPositive = false;
    bool sawNegativeAfterPositive = false;
    for (uint64_t t = 0; t <= 3 * params.durationUs; t += stepUs) {
        const Sample s = source.sampleAt(t);
        if (s.gz > 0) {
            sawPositive = true;
        }
        if (s.gz < 0 && sawPositive) {
            sawNegativeAfterPositive = true;
        }
    }

    EXPECT_TRUE(sawPositive);
    EXPECT_TRUE(sawNegativeAfterPositive);
    EXPECT_TRUE(isBaseline(source.sampleAt(4 * params.durationUs),
                           params.accelRestZ));
}

TEST(ImuFusionSourceSynthetic, ResetRestartsTheSequence)
{
    ImuFusionSource source;

    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
    EXPECT_EQ(source.triggerSwing(), SwingType::BACKHAND);
    source.sampleAt(0);

    source.reset();

    EXPECT_EQ(source.swingsTriggered(), 0u);
    EXPECT_EQ(source.triggerSwing(), SwingType::FOREHAND);
}

TEST(ImuFusionSourceSynthetic, DeterministicAcrossRuns)
{
    const ImuFusionSource::SwingParams params{};
    const uint64_t stepUs = 5 * kMs;

    auto run = [&] {
        ImuFusionSource     source;
        std::vector<Sample> out;
        source.triggerSwing();
        for (uint64_t t = 0; t <= 2 * params.durationUs; t += stepUs) {
            out.push_back(source.sampleAt(t));
        }
        return out;
    };

    const auto first  = run();
    const auto second = run();

    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].ax, second[i].ax) << "sample " << i;
        EXPECT_EQ(first[i].gz, second[i].gz) << "sample " << i;
    }
}

} // namespace
