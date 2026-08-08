// Unit tests for Sensor::SampleRateAdapter, the per-listener decimation gate.
//
// The delivery rule and its derivation live on the class itself, in
// SDK/Simulator/Components/SampleRateAdapter.hpp. This file asserts that rule
// rather than restating it. Every figure quoted in prose about this adapter
// appears below, so prose that drifts from the code fails a test.
//
// These tests pin the behaviour as found; they are not a claim that it is
// desirable.

#include <gtest/gtest.h>

#include "SDK/Simulator/Components/SampleRateAdapter.hpp"

#include <cstdint>
#include <vector>

using Sensor::SampleRateAdapter;

namespace {

// A fresh adapter starts with lastEmit == lastSample == 0, so the opening
// samples of any stream see an inflated UP and emit out of turn. Discarding a
// prefix and counting whole decimation cycles keeps every count below exact
// rather than approximate.
constexpr uint64_t kWarmUpSamples = 64;

uint64_t passedOf(uint64_t periodUs, uint64_t spacingUs, uint64_t count,
                  uint64_t baseUs = 60u * 1000u * 1000u)
{
    SampleRateAdapter sra(periodUs);
    uint64_t passed = 0;

    for (uint64_t i = 0; i < kWarmUpSamples + count; ++i) {
        const bool emit = sra.shouldEmit(baseUs + i * spacingUs);
        if (i >= kWarmUpSamples && emit) {
            ++passed;
        }
    }

    return passed;
}

// The closed form, spelled out independently of the adapter so the tests compare
// two expressions of the rule rather than one against itself.
uint64_t oneInN(uint64_t periodUs, uint64_t spacingUs)
{
    if (spacingUs == 0) {
        return 0; // not meaningful for a fixed spacing; see SharedArrivalInstant
    }
    const uint64_t n = periodUs / spacingUs;
    return n < 1 ? 1 : n;
}

// Samples consumed per survivor, as observed.
uint64_t observedN(uint64_t periodUs, uint64_t spacingUs, uint64_t cycles = 400)
{
    const uint64_t n = oneInN(periodUs, spacingUs);
    const uint64_t count = n * cycles;
    const uint64_t passed = passedOf(periodUs, spacingUs, count);
    EXPECT_GT(passed, 0u) << "period=" << periodUs << "us spacing=" << spacingUs << "us";
    return passed == 0 ? 0 : count / passed;
}

// A heart rate as the spacing a producer stamping true beat instants would
// emit. Whole microseconds, that being the resolution setTimestampUs() offers.
uint64_t spacingForBpm(unsigned bpm)
{
    return (60u * 1000u * 1000u + bpm / 2) / bpm;
}

} // namespace

// Walks every band edge EP/k and the microsecond either side of it, where all
// the boundary behaviour lives, plus a coprime stride so the sweep is not made
// entirely of special cases.
TEST(SampleRateAdapter, ClosedFormHoldsOverSpacingSweep)
{
    // 840000 is divisible by every k in 1..8, so its low band edges are exact.
    // 1000001 is prime to all of them, so none of its edges are.
    const uint64_t periods[] = {1000, 250000, 500000, 840000, 1000000, 1000001, 5000000};

    uint64_t checked = 0;

    for (uint64_t ep : periods) {
        std::vector<uint64_t> spacings;

        for (uint64_t k = 1; k <= 64; ++k) {
            const uint64_t edge = ep / k;
            if (edge == 0) {
                break;
            }
            spacings.push_back(edge);
            spacings.push_back(edge + 1);
            if (edge > 1) {
                spacings.push_back(edge - 1);
            }
        }

        // Spaced wider than the period, and a handful of arbitrary spacings.
        spacings.push_back(ep + 1);
        spacings.push_back(2 * ep);
        for (uint64_t d = ep / 64; d < ep + ep / 8; d += ep / 37 + 1) {
            spacings.push_back(d);
        }

        for (uint64_t d : spacings) {
            const uint64_t n = oneInN(ep, d);
            const uint64_t count = n * 40;
            ASSERT_EQ(passedOf(ep, d, count), count / n)
                << "period=" << ep << "us spacing=" << d << "us expected 1 in " << n;
            ++checked;
        }
    }

    // Guard against the sweep silently collapsing to nothing.
    EXPECT_GT(checked, 1000u);
}

// The base instant only affects the warm-up, which every measurement discards.
TEST(SampleRateAdapter, BaseInstantDoesNotMatter)
{
    const uint64_t bases[] = {0, 1, 999, 1000000, 86400ull * 1000000ull};

    for (uint64_t base : bases) {
        EXPECT_EQ(passedOf(1000000, 500000, 800, base), 400u) << "base=" << base;
        EXPECT_EQ(passedOf(1000000, 600000, 800, base), 800u) << "base=" << base;
    }
}

// Half the period, not one period -- the boundary an intuitive reading of the
// gate misses. Checked at several periods, since the figure is easy to fix to
// whichever one happened to be worked through.
TEST(SampleRateAdapter, ThinningThresholdIsHalfThePeriod)
{
    const uint64_t periods[] = {1000, 250000, 500000, 1000000, 5000000};

    for (uint64_t ep : periods) {
        const uint64_t half = ep / 2;

        // Spaced further apart than the period: nothing to thin.
        EXPECT_EQ(observedN(ep, ep + 1), 1u) << "period=" << ep;
        EXPECT_EQ(observedN(ep, ep), 1u) << "period=" << ep;

        // Already faster than one per period, yet untouched: the
        // counter-intuitive half of the boundary.
        EXPECT_EQ(observedN(ep, half + 1), 1u) << "period=" << ep;
        EXPECT_EQ(observedN(ep, half + (ep / 8)), 1u) << "period=" << ep;

        EXPECT_EQ(observedN(ep, half), 2u) << "period=" << ep;
        EXPECT_EQ(observedN(ep, half - 1), 2u) << "period=" << ep;
    }
}

// Reachable rather than theoretical, because timestamps are whole microseconds
// -- and it is easy to put the edge on the wrong side.
TEST(SampleRateAdapter, ExactRatioBelongsToTheThinnerBand)
{
    constexpr uint64_t kEp = 840000; // divisible by every n below, so EP/n is exact

    for (uint64_t n = 2; n <= 8; ++n) {
        ASSERT_EQ(kEp % n, 0u) << "pick a period divisible by n for an exact ratio";
        const uint64_t d = kEp / n;

        EXPECT_EQ(observedN(kEp, d), n) << "spacing=" << d;
        EXPECT_EQ(observedN(kEp, d + 1), n - 1) << "spacing=" << d + 1;
    }
}

// Predicting from a bpm figure and a floating-point divide gets this pair
// backwards: the operands are integer microseconds, so 6.99999 periods floors
// to 6.
TEST(SampleRateAdapter, ExactIntegerDivisionNotRealRatio)
{
    constexpr uint64_t kEp = 5000000; // 5000 ms; 5000000/7 == 714285.71...

    EXPECT_EQ(observedN(kEp, 714286), 6u);
    EXPECT_EQ(observedN(kEp, 714285), 7u);
    EXPECT_EQ(observedN(kEp, 714287), 6u);
}

// A staircase in the rate, not a curve: reading the gate as roughly
// proportional to rate makes it sound far smoother than it is.
TEST(SampleRateAdapter, ThinningIsQuantisedIntoBands)
{
    constexpr uint64_t kEp = 1000000;

    for (uint64_t n = 1; n <= 6; ++n) {
        const uint64_t top    = kEp / n;           // in the band, thinnest spacing kept
        const uint64_t bottom = kEp / (n + 1) + 1; // still in the band, one us above the next
        ASSERT_LE(bottom, top);

        EXPECT_EQ(observedN(kEp, top), n) << "n=" << n;
        EXPECT_EQ(observedN(kEp, bottom), n) << "n=" << n;
    }

    // 121 and 179 bpm are nearly 50% apart in rate and lose the same half.
    EXPECT_EQ(observedN(kEp, spacingForBpm(121)), 2u);
    EXPECT_EQ(observedN(kEp, spacingForBpm(179)), 2u);

    // 239 bpm shares a band with 181, not with 121: bands narrow as rate rises.
    EXPECT_EQ(observedN(kEp, spacingForBpm(181)), 3u);
    EXPECT_EQ(observedN(kEp, spacingForBpm(239)), 3u);
}

// The 1000 ms case a producer falls into by leaving the period at its default.
// These are the figures ExternalSensors.md and SensorDataParserRrInterval.hpp
// point at, so changing one there without changing it here fails.
TEST(SampleRateAdapter, HeartRateTableAgainstAOneSecondPeriod)
{
    constexpr uint64_t kEp = 1000000;

    struct Row { unsigned bpm; uint64_t oneIn; };
    const Row rows[] = {
        { 40, 1}, { 60, 1}, {100, 1}, {119, 1}, // spaced above 500 ms: untouched
        {120, 2}, {140, 2}, {179, 2},           // 120..179 bpm: half
        {180, 3}, {239, 3},                     // 180..239 bpm: two thirds
        {240, 4},                               // 240 bpm: three quarters
        {300, 5},
    };

    for (const Row& r : rows) {
        EXPECT_EQ(observedN(kEp, spacingForBpm(r.bpm)), r.oneIn)
            << r.bpm << " bpm (spacing " << spacingForBpm(r.bpm) << "us)";
    }
}

// A driver registering 250 ms -- one shortest-plausible heartbeat -- touches
// nothing physiological, which is the reason to register a period that short.
TEST(SampleRateAdapter, QuarterSecondPeriodPassesEveryRealHeartRate)
{
    constexpr uint64_t kEp = 250000;

    for (unsigned bpm = 30; bpm <= 240; ++bpm) {
        const uint64_t d = spacingForBpm(bpm);
        EXPECT_EQ(passedOf(kEp, d, 500), 500u)
            << bpm << " bpm (spacing " << d << "us) should be untouched";
    }

    // The margin it leaves.
    EXPECT_EQ(observedN(kEp, 125001), 1u);
    EXPECT_EQ(observedN(kEp, 125000), 2u); // 480 bpm
}

// What a driver has to register to keep a stream whole: at a shortest expected
// spacing dMin, EP <= 2*dMin - 1. Worth stating because the safe period is
// nearly twice what "one sample per period" would suggest.
TEST(SampleRateAdapter, MinPeriodRequirementForUnthinnedBeats)
{
    constexpr uint64_t kShortestRealInterval = 250000; // 250 ms == 240 bpm

    EXPECT_EQ(observedN(499000, kShortestRealInterval), 1u); // 499 ms: just holds
    EXPECT_EQ(observedN(500000, kShortestRealInterval), 2u); // 500 ms: already halves
    EXPECT_EQ(observedN(250000, kShortestRealInterval), 1u); // 250 ms: holds with margin

    // The general form, over a range of candidate shortest intervals.
    for (uint64_t dMin = 200000; dMin <= 400000; dMin += 10000) {
        EXPECT_EQ(observedN(2 * dMin - 1, dMin), 1u) << "dMin=" << dMin;
        EXPECT_EQ(observedN(2 * dMin, dMin), 2u) << "dMin=" << dMin;
    }
}

// What a producer stamping arrival instead of beat instants gets: every beat of
// one notification shares a timestamp, so one survives per notification however
// many arrive, then the notifications thin by their own spacing.
TEST(SampleRateAdapter, SharedArrivalInstantKeepsOneOfN)
{
    constexpr uint64_t kEp = 1000000;
    constexpr uint64_t kBursts = 200;

    for (uint64_t n : {uint64_t{1}, uint64_t{2}, uint64_t{3}, uint64_t{5}, uint64_t{40}}) {
        for (uint64_t burstSpacing : {uint64_t{2000000}, uint64_t{1000000}, uint64_t{500000}}) {
            SampleRateAdapter sra(kEp);
            uint64_t passed = 0;

            for (uint64_t b = 0; b < kWarmUpSamples + kBursts; ++b) {
                for (uint64_t j = 0; j < n; ++j) {
                    const bool emit = sra.shouldEmit(kEp * 100 + b * burstSpacing);
                    if (b >= kWarmUpSamples && emit) {
                        ++passed;
                    }
                }
            }

            const uint64_t perBurst = oneInN(kEp, burstSpacing);
            EXPECT_EQ(passed, kBursts / perBurst)
                << n << " samples per instant, bursts " << burstSpacing << "us apart";
            EXPECT_EQ(kBursts * n / (passed == 0 ? 1 : passed), n * perBurst)
                << "expected 1 in " << n * perBurst;
        }
    }
}

// The degenerate end of the case above, and why a replay mock that stamps
// arrival delivers one beat per notification whatever period it registers.
TEST(SampleRateAdapter, RepeatedIdenticalInstantPassesOnce)
{
    for (uint64_t ep : {uint64_t{1000}, uint64_t{250000}, uint64_t{1000000}}) {
        SampleRateAdapter sra(ep);
        const uint64_t now = ep * 100;

        uint64_t passed = 0;
        for (int i = 0; i < 1000; ++i) {
            if (sra.shouldEmit(now)) {
                ++passed;
            }
        }

        EXPECT_EQ(passed, 1u) << "period=" << ep;
    }
}
