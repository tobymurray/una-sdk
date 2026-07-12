// Reproduction / regression harness for the lock-order inversion between
// Sensor::Driver::mMutex and Sensor::Manager::mMutex.
//
//   Order A (service thread): Driver::connect()/disconnect() hold the driver
//     mutex (SensorDriver.cpp:117 / :246) and, still holding it, call into the
//     manager (regSensor/unRegSensor/updatePeriod), which take the manager
//     mutex.  => driver-mutex THEN manager-mutex.
//
//   Order B (manager thread): Manager::thread() holds the manager mutex across
//     the whole refresh pass (SensorManager.cpp:301-315) and calls
//     sensorRefresh() -> Driver::pushDataSample(), which takes the driver mutex
//     (SensorDriver.cpp:81).  => manager-mutex THEN driver-mutex.
//
// Both mutexes are plain std::mutex (OS.cpp), so the two orders form a genuine
// ABBA deadlock.
//
// Two tests:
//   * InversionObservable  - short churn, no injected delay; always completes.
//     Under ThreadSanitizer it deterministically reports lock-order-inversion
//     because both acquisition orders execute concurrently.
//   * DeadlockManifests    - injects a small delay INSIDE sensorRefresh, while
//     the manager already holds its mutex and BEFORE pushDataSample() takes the
//     driver mutex.  The delay adds NO new lock; it only widens the pre-existing
//     window during which the manager holds M and is about to grab D, so a
//     concurrent Order-A thread (holding D, waiting for M) closes the cycle.
//     A watchdog fails loudly instead of hanging forever.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "SDK/Simulator/Components/SensorManager.hpp"
#include "SDK/Simulator/Components/SensorDriver.hpp"
#include "SDK/Simulator/Components/Sensors/ISensor.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"

#include "support/SimSystemDouble.hpp"

namespace {

using namespace std::chrono_literals;

// ---- phase markers, dumped by the watchdog on a hang -----------------------
std::atomic<uint64_t> gWorkerIter{0};
std::atomic<int>      gWorkerPhase{0}; // 0 idle, 1 connecting L2, 2 disconnecting L2
std::atomic<uint64_t> gRefreshCount{0};

// Injected widening delay (microseconds) applied at the top of sensorRefresh,
// while the manager holds its mutex, before pushDataSample() grabs the driver
// mutex. 0 == no injection.
std::atomic<uint32_t> gWidenUs{0};

// ---- test doubles ----------------------------------------------------------
class FakeListener : public SDK::Interface::ISensorDataListener {
public:
    void onSdlNewData(uint16_t, const SDK::Sensor::Data*, uint16_t, uint16_t) override
    {
        mDeliveries.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<uint64_t> mDeliveries{0};
};

class FakeCtrl : public Sensor::ISensorDriverCtrl {
public:
    float sdcStart(Sensor::Driver*, float period) override { return period; }
    void  sdcStop(Sensor::Driver*) override {}
    float sdcUpdatePeriod(Sensor::Driver*, float period) override { return period; }
    float sdcGetMinPeriod(Sensor::Driver*) override { return 0.5f; }
    const char* sdcGetDescription(Sensor::Driver*) override { return "fake"; }
};

class FakeSensor : public Interface::ISensor {
public:
    explicit FakeSensor(FakeCtrl& ctrl)
        : mDriver(*this,
                  SDK::Sensor::Type::FUSION_RAW,
                  /*fieldCount*/ 6,
                  ctrl,
                  Sensor::Driver::Mode::PERIODIC_BASED)
    {
    }

    Sensor::Driver& driver() { return mDriver; }

    void sensorRefresh() override
    {
        // Widen the legitimately-existing window: the manager already holds ITS
        // mutex here (SensorManager.cpp:301) and pushDataSample() below is what
        // acquires the driver mutex. Sleeping adds no lock of its own.
        const uint32_t w = gWidenUs.load(std::memory_order_relaxed);
        if (w != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(w));
        }

        auto& sample = mDriver.getDataSample();
        sample.setTimestamp(++mTick);
        mDriver.pushDataSample();

        gRefreshCount.fetch_add(1, std::memory_order_relaxed);
    }

private:
    Sensor::Driver mDriver;
    uint32_t       mTick{0};
};

// Runs the Order-A churn: L1 stays connected (keeps the sensor registered so the
// manager thread keeps refreshing = Order B); L2 repeatedly connects with a
// faster period (drives updatePeriod->manager mutex while holding the driver
// mutex) and disconnects (drives applyPeriod->manager mutex likewise).
void churn(Sensor::Driver& driver, FakeListener& l2, std::chrono::milliseconds budget)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    uint64_t   iter     = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        gWorkerPhase.store(1, std::memory_order_relaxed);
        driver.connect(&l2, /*period ms*/ 1.0f, /*latency*/ 0);

        gWorkerPhase.store(2, std::memory_order_relaxed);
        driver.disconnect(&l2);

        gWorkerIter.store(++iter, std::memory_order_relaxed);
    }
    gWorkerPhase.store(0, std::memory_order_relaxed);
}

[[noreturn]] void reportDeadlockAndExit()
{
    std::fprintf(stderr,
        "\n[ DEADLOCK ] watchdog fired: lock-order inversion closed the cycle.\n"
        "  worker phase   : %d (1=connect L2 / 2=disconnect L2)\n"
        "  worker iters   : %llu\n"
        "  refresh passes : %llu\n"
        "  The service thread holds the driver mutex and is blocked on the\n"
        "  manager mutex, while the manager thread holds the manager mutex and\n"
        "  is blocked on the driver mutex inside pushDataSample().\n\n",
        gWorkerPhase.load(),
        static_cast<unsigned long long>(gWorkerIter.load()),
        static_cast<unsigned long long>(gRefreshCount.load()));
    std::fflush(stderr);
    // The worker and manager threads are wedged; a normal return would hang the
    // process on join()/singleton teardown. Exit non-zero with the diagnostic.
    std::_Exit(70);
}

// ---- global env: stop the singleton manager thread at process teardown -----
class ManagerEnv : public ::testing::Environment {
public:
    void TearDown() override
    {
        // Let Manager::thread() return so the singleton destructor's join()
        // does not block at process exit.
        simtest::setManagerRunning(false);
        std::this_thread::sleep_for(700ms);
    }
};

const bool kEnvRegistered = [] {
    ::testing::AddGlobalTestEnvironment(new ManagerEnv());
    return true;
}();

// ---- tests -----------------------------------------------------------------

// Both lock orders execute concurrently. Completes quickly; under TSan it emits
// a lock-order-inversion (potential deadlock) report.
TEST(SensorLockOrder, InversionObservable)
{
    gWidenUs.store(0, std::memory_order_relaxed);

    FakeCtrl     ctrl;
    FakeSensor   sensor(ctrl);
    FakeListener l1;
    FakeListener l2;

    // First listener: registers the sensor -> manager thread starts refreshing.
    ASSERT_TRUE(sensor.driver().connect(&l1, /*period*/ 4.0f, /*latency*/ 0));

    std::atomic<bool> done{false};
    std::thread worker([&] {
        churn(sensor.driver(), l2, 1500ms);
        done.store(true, std::memory_order_release);
    });

    // Watchdog: this test must NOT deadlock (widen == 0). If it does, fail.
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            reportDeadlockAndExit();
        }
        std::this_thread::sleep_for(20ms);
    }
    worker.join();

    sensor.driver().disconnect(&l1);
    EXPECT_GT(gRefreshCount.load(), 0u) << "manager thread never refreshed";
}

// Injects a widening delay so the ABBA window is easy to hit. A watchdog turns a
// real deadlock into a loud failure instead of an infinite hang. After a fix
// that removes the inversion by construction, this completes cleanly.
TEST(SensorLockOrder, DeadlockManifests)
{
    gWidenUs.store(800, std::memory_order_relaxed); // 0.8 ms, no new lock

    FakeCtrl     ctrl;
    FakeSensor   sensor(ctrl);
    FakeListener l1;
    FakeListener l2;

    ASSERT_TRUE(sensor.driver().connect(&l1, /*period*/ 4.0f, /*latency*/ 0));

    std::atomic<bool> done{false};
    std::thread worker([&] {
        churn(sensor.driver(), l2, 2000ms);
        done.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            reportDeadlockAndExit(); // never returns
        }
        std::this_thread::sleep_for(20ms);
    }
    worker.join();

    gWidenUs.store(0, std::memory_order_relaxed);
    sensor.driver().disconnect(&l1);
    EXPECT_GT(gWorkerIter.load(), 0u);
}

} // namespace
