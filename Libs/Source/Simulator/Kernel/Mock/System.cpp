
#include "SDK/Simulator/Kernel/Mock/System.hpp"
#include <cstdint>

#ifndef _WIN32
#include <time.h>
static void posix_sleep_ms(uint32_t ms) {
    struct timespec ts { static_cast<time_t>(ms / 1000), static_cast<long>((ms % 1000) * 1000000L) };
    nanosleep(&ts, nullptr);
}
#endif

#define LOG_MODULE_PRX      "Mock.System"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace SDK::Simulator::Mock
{
    bool SystemGUI::mAppRunning = true;

    System::System()
    {
        GetTimeMs();

    }

    uint32_t System::GetTimeMs()
    {
        static auto start = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
    }

    ////////////////////////////////////
	//// SystemGUI implementation
    ////////////////////////////////////

    bool SystemGUI::isAppRunning()
    {
        return mAppRunning;
	}

    void SystemGUI::exit(int status)
    {
        LOG_DEBUG("status = %d\n", status);

        mAppRunning = false;

        static_cast<touchgfx::HALSDL2*>(touchgfx::HAL::getInstance())->stopApplication();
    }

    uint32_t SystemGUI::getTimeMs()
    {
        return System::GetTimeMs();
    }

    void SystemGUI::delay(uint32_t ms)
    {
#ifdef _WIN32
        Sleep(ms);
#else
        posix_sleep_ms(ms);
#endif
    }

    void SystemGUI::yield()
    {}

    ////////////////////////////////////
    //// SystemService implementation
    ////////////////////////////////////

    bool SystemService::isAppRunning() const
    {
        return mAppRunning;
    }

    void SystemService::exit(int status)
    {
        LOG_DEBUG("status = %d\n", status);

        mAppRunning = false;
    }

    uint32_t SystemService::getTimeMs()
    {
        return System::GetTimeMs();
    }

    void SystemService::delay(uint32_t ms)
    {
#ifdef _WIN32
        Sleep(ms);
#else
        posix_sleep_ms(ms);
#endif
    }

    void SystemService::yield()
    {}

} // namespace SDK::Simulator::Mock