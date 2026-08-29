/**
 ******************************************************************************
 * @file    BacklightStub.cpp
 * @date    14-July-2025
 * @author  Oleksandr Tymoshenko <oleksandr.tymoshenko@droid-technologies.com>
 * @brief   Backlight interface stub.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "SDK/Simulator/Kernel/Mock/Backlight.hpp"

#define LOG_MODULE_PRX      "Mock.Backlight"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SDK::Simulator::Mock {

    bool Backlight::on(uint32_t timeout)
    {
        LOG_INFO("on backlight, timeout = %u\n", timeout);
        m_isOn = true;

        // A pending auto-off from an earlier call is always cancelled, whatever
        // this call asks for.
        if (mTimer.isActive(mTimerId)) {
            mTimer.stop(mTimerId);
        }

        // Zero means no automatic turn-off, as IBacklight::on and
        // RequestBacklightSet::autoOffTimeoutMs both document it. Starting a
        // zero-length timer instead fires on the next poll and blanks the
        // backlight roughly 50 ms later, which is the opposite of what was asked.
        //
        // Confirmed on hardware 2026-08-27: with autoOffTimeoutMs = 0 the watch
        // holds the backlight on indefinitely.
        if (timeout == 0) {
            return true;
        }

        mTimerId = mTimer.start(timeout, std::bind(&Backlight::timerCallback, this));
        return true;
    }

    bool Backlight::off()
    {
        LOG_INFO("off backlight\n");
        m_isOn = false;
        return true;
    }

    bool Backlight::isOn()
    {
        LOG_INFO("called\n");
        return m_isOn;
    }

    void Backlight::timerCallback()
    {
        off();
    }
}