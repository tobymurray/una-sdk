/**
 ******************************************************************************
 * @file    SampleRateAdapter.hpp
 * @date    18-September-2025
 * @author  Oleksandr Tymoshenko <oleksandr.tymoshenko@droid-technologies.com>
 * @brief   Sample rate adapter
 *
 ******************************************************************************
 */

#ifndef __SENSOR_SAMPLE_RATE_ADAPTER_HPP
#define __SENSOR_SAMPLE_RATE_ADAPTER_HPP

#include <cstdint>

namespace Sensor
{
/**
 * @brief Per-listener decimation gate. Sensor::DataQueue::pushData() asks this
 *        whether to forward each incoming sample on to its listener.
 *
 * DELIVERY RULE, stated here once and cited rather than copied elsewhere. For
 * samples at a fixed spacing d against an expected period EP, one in n passes:
 *
 *     n = max(1, EP / d)      // INTEGER division; both in MICROSECONDS
 *
 * Since SP == d at a fixed spacing, `UP >= EP` is subsumed by `UP + SP > EP` and
 * the gate reduces to `UP > EP - d`. UP climbs in steps of d from zero after
 * each emit, so the next emit is the first k with (k+1)d > EP. Three
 * consequences, each easy to get wrong by reading a rate threshold off a single
 * worked example:
 *
 *  - The boundary is HALF the period, not one period: n reaches 2 only at
 *    d <= EP/2, so a stream already arriving faster than one per period still
 *    passes whole. Adding SP is what buys that -- it emits the sample nearest a
 *    deadline rather than the first one past it, a whole spacing early.
 *  - An exact ratio falls in the THINNER band: d == EP/2 passes half, one
 *    microsecond more passes everything. Timestamps are whole microseconds, so
 *    that edge is reachable rather than theoretical.
 *  - Thinning is QUANTISED: every d in (EP/(n+1), EP/n] loses the same
 *    fraction, so against EP = 1000 ms both 121 and 179 bpm lose half.
 *
 * Samples sharing an arrival instant (d == 0) need no separate rule: SP drops
 * out, the gate becomes "at least EP since the last emit", and an emit zeroes
 * that at the same instant, so one of N survives -- then the bursts thin by
 * their own spacing, giving 1 in N * max(1, EP/D) for bursts D apart.
 *
 * UNITS ARE MICROSECONDS throughout — note the 1000 default is 1 ms, not the 1 s
 * it reads as. Sensor::DataQueue calls setExpectedPeriod(periodMs * 1000) and
 * passes the frame timestamp, not wall-clock, as
 * mTimeStamp * 1000 + mTimeStampUs. Hence an integer division above rather than
 * a ratio of durations, which is not pedantry: against 5000 ms a 714286 us
 * spacing passes 1 in 6, not 1 in 7.
 *
 * Tests/Host/simulator/SampleRateAdapter_test.cpp asserts every figure above.
 */
class SampleRateAdapter
{
public:
    /**
     * @brief Constructor
     * @param expectedPeriod Desired period between emitted samples, in
     *        MICROSECONDS (so the 1000 default is 1 ms, not 1 s)
     */
    explicit SampleRateAdapter(uint64_t expectedPeriod = 1000);

    /**
     * @brief Set the expected sampling period.
     * @param periodUs Period in MICROSECONDS
     */
    void setExpectedPeriod(uint64_t periodUs);

    /**
     * @brief Reset the internal state (e.g., after reconnect)
     */
    void reset();

    /**
     * @brief Check whether the sample should be forwarded to listener
     * @return true if the sample passes the delivery rule above
     */
    bool shouldEmit();

    /**
     * @brief Check whether the sample stamped @p ts should be forwarded.
     * @param ts Sample timestamp in MICROSECONDS, from the frame rather than
     *        from a clock read at call time
     * @return true if the sample passes the delivery rule above
     */
    bool shouldEmit(uint64_t ts);

private:
    uint64_t mExpectedPeriod;       ///< Desired period in us
    uint64_t mLastEmitedTimestamp;  ///< Timestamp of last emitted sample, us
    uint64_t mLastSampleTimestamp;  ///< Timestamp of last input sample, us
};

} // namespace Sensor

#endif // __SENSOR_SAMPLE_RATE_ADAPTER_HPP
