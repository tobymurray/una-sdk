/**
 ******************************************************************************
 * @file    Logger.hpp
 * @date    14-July-2025
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Logger interface implementation for simulator.
 *
 ******************************************************************************
 *
 * This header deliberately does NOT include SDK/UnaLogger/Logger.h and contains
 * no LOG_* call and no call into the logging component. Two consequences, both
 * intentional:
 *
 *  - Nothing in this header's inline bodies depends on the including translation
 *    unit's LOG_LEVEL, so every TU compiles byte-identical definitions. The
 *    previous version called Logger_deinit() from an inline destructor, which
 *    gave a no-log TU a different body from everyone else -- one inline entity
 *    with two definitions is an ODR violation, and the linker silently keeps an
 *    arbitrary one.
 *
 *  - Simulator/Kernel/Kernel.hpp -- and therefore every simulator TU -- is no
 *    longer in the set of headers that transitively pull in Logger.h, so a .cpp
 *    can still set LOG_MODULE_PRX / LOG_MODULE_LEVEL after its other includes.
 *
 ******************************************************************************
 */

#pragma once

#include "SDK/Interfaces/ILogger.hpp"
#include "SDK/Simulator/OS/OS.hpp"
#include "touchgfx/Utils.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <new>

#ifndef _WIN32
// GetTickCount64() is Windows-only. Provide a portable wrapper.
static inline uint64_t GetTickCount64()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}
#endif

namespace SDK::Simulator::Mock
{

/**
 * @brief Implementation of SDK::Interface::ILogger for the simulator.
 *
 * Not constructible by callers: obtain the single instance from logger(). That
 * instance has static storage duration and is never destroyed, which is exactly
 * what an installed sink must guarantee. It used to be a member of
 * SDK::Simulator::Kernel, giving it a lifetime shorter than the threads and
 * static-duration singletons that log through it.
 *
 * The serialising mutex is a non-static member, so the one immortal Logger owns
 * the one immortal mutex. As a static member it would have been destroyed during
 * static teardown while background threads were still logging -- the same
 * lifetime bug one level down.
 */
class Logger final : public SDK::Interface::ILogger {
public:

    void printf(const char* format, ...) override
        __attribute__((format(printf, 2, 3)))
    {
        va_list args;
        va_start(args, format);
        render(nullptr, nullptr, nullptr, 0, format, args);
        va_end(args);
    }

    void vprintf(const char* format, va_list args) override
        __attribute__((format(printf, 2, 0)))
    {
        render(nullptr, nullptr, nullptr, 0, format, args);
    }

    void mvprintf(const char* level, const char* module_name, const char* func, int line,
        const char* fmt, va_list args) override
        __attribute__((format(printf, 6, 0)))
    {
        render(level, module_name, func, line, fmt, args);
    }

private:

    Logger() = default;
    ~Logger() override = default;

    friend Logger& logger();

    OS::Mutex mMutexLog;

    /**
     * @brief Render one message to stdout.
     *
     * Every scratch buffer is a local. The previous version used function-local
     * statics for the level and metadata fields, which made this non-reentrant:
     * a log call reached from inside a log call -- from an assert handler or an
     * allocator hook -- overwrote the outer call's buffers even single-threaded.
     */
    void render(const char* level, const char* module_name, const char* func, int line,
                const char* fmt, va_list args)
    {
        OS::MutexCS cs(mMutexLog);

        const uint32_t time = static_cast<uint32_t>(GetTickCount64());

        char meta[64];
        int  idx = 0;
        if (module_name != nullptr) {
            idx += snprintf(&meta[idx], sizeof(meta) - static_cast<size_t>(idx), "%s::", module_name);
        }
        if (func != nullptr) {
            idx += snprintf(&meta[idx], sizeof(meta) - static_cast<size_t>(idx), "%s", func);
        }
        if (line != 0) {
            idx += snprintf(&meta[idx], sizeof(meta) - static_cast<size_t>(idx), "%s%d",
                            func != nullptr ? "::" : "", line);
        }

        char userMsg[2048];
        vsnprintf(userMsg, sizeof(userMsg), fmt, args);

        if (idx > 0) {
            char levelBuff[10] = { 0 };
            if (level != nullptr) {
                snprintf(levelBuff, sizeof(levelBuff), "-%s- ", level);
            }
            touchgfx_printf("%10u %s%-36s: %s", time, levelBuff, meta, userMsg);
        } else {
            // The old form was touchgfx_printf("%s%s%%s", timeBuff, level, userMsg):
            // it fed a null `level` to %s and dropped userMsg entirely. Reachable
            // through ILogger::printf()/vprintf(), which pass null metadata.
            touchgfx_printf("%10u %s", time, userMsg);
        }
    }
};

/**
 * @brief The one simulator sink. Never destroyed.
 *
 * Inline, so there is a single instance across the program without adding a new
 * translation unit -- the simulator Makefiles list SDK sources explicitly, so a
 * new .cpp would mean editing 14 of them.
 *
 * Placement-new rather than `static Logger instance;` because a virtual
 * destructor is never trivial: the plain form would register a __cxa_atexit
 * handler and destroy the sink during static teardown, after main()'s locals but
 * before every thread has stopped. Only the pointer has a dynamic initialiser,
 * and a pointer is trivially destructible.
 */
inline Logger& logger()
{
    alignas(Logger) static unsigned char storage[sizeof(Logger)];
    static Logger* instance = new (static_cast<void*>(storage)) Logger();
    return *instance;
}

} // namespace SDK::Simulator::Mock
