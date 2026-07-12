// Host-test replacement for Libs/Source/Simulator/Kernel/Mock/System.cpp.
//
// The production System.cpp implements SystemGUI::exit() by calling into the
// TouchGFX HALSDL2 instance, which does not exist in a headless test binary.
// This double implements the same class methods (a static data member and the
// class's own member functions may be defined in any translation unit,
// regardless of access) but backs isAppRunning() with a test-controllable gate
// so a test can stop the SensorManager thread deterministically.

#include "SDK/Simulator/Kernel/Mock/System.hpp"
#include "SimSystemDouble.hpp"

#include <atomic>
#include <chrono>

namespace {
// Internal-linkage gate consulted by isAppRunning(). Starts true so the manager
// thread runs as soon as a sensor is registered.
std::atomic<bool> gManagerRunning{true};
} // namespace

namespace SDK::Simulator::Mock {

// Required definition of the class-declared static (unused by this double, but
// the symbol must exist because the header declares it).
bool SystemGUI::mAppRunning = true;

bool SystemGUI::isAppRunning()
{
    return gManagerRunning.load(std::memory_order_acquire);
}

void SystemGUI::exit(int /*status*/)
{
    gManagerRunning.store(false, std::memory_order_release);
}

uint32_t SystemGUI::getTimeMs()
{
    return System::GetTimeMs();
}

void SystemGUI::delay(uint32_t /*ms*/)
{
}

void SystemGUI::yield()
{
}

uint32_t System::GetTimeMs()
{
    static auto start = std::chrono::steady_clock::now();
    auto        now   = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
}

} // namespace SDK::Simulator::Mock

namespace simtest {

void setManagerRunning(bool running)
{
    gManagerRunning.store(running, std::memory_order_release);
}

bool managerRunning()
{
    return gManagerRunning.load(std::memory_order_acquire);
}

} // namespace simtest
