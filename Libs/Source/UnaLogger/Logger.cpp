/**
 ******************************************************************************
 * @file    Logger.cpp
 * @date    04-10-2025
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Logger component for application.
 ******************************************************************************
 *
 * The sink pointer is atomic and is never cleared. Both properties are load
 * bearing:
 *
 *  - atomic, because a background thread can be inside message() while another
 *    thread installs a sink. A non-atomic pointer here is a data race even when
 *    the window looks impossibly small.
 *
 *  - never cleared, because "clear it before the sink dies" cannot be enforced.
 *    The sink must outlive the process instead; installSink() says so in its
 *    name. There is no uninstall entry point to get wrong.
 *
 * Zero-initialised, so no dynamic initialiser and no .init_array dependency, and
 * no static destructor -- which matters on the embedded target, where the
 * __cxa_atexit table is a fixed 32 slots and __cxa_guard_acquire is not
 * thread safe.
 *
 ******************************************************************************
 */

#include "SDK/UnaLogger/Logger.h"

#include <atomic>
#include <stdarg.h>
#include <stdlib.h>

namespace SDK::Log
{

namespace
{

/**
 * @brief Installed sink. Zero-initialised; published atomically; never cleared.
 */
std::atomic<SDK::Interface::ILogger*> sSink{nullptr};

/**
 * @brief Runtime ceiling. Starts at the compile-time ceiling.
 */
std::atomic<uint8_t> sRuntimeLevel{static_cast<uint8_t>(LOG_LEVEL)};

/**
 * @brief Messages discarded because no sink was installed yet.
 */
std::atomic<uint32_t> sDropped{0};

/**
 * @brief Legacy single-character tag expected by SDK::Interface::ILogger.
 *
 * ILogger is the ABI boundary with the host firmware, so its signature keeps
 * taking a string. The enum-to-string mapping is confined to here.
 */
const char* tagOf(Level level)
{
    switch (level) {
    case Level::Error:   return "E";
    case Level::Warning: return "W";
    case Level::Info:    return "I";
    case Level::Debug:   return "D";
    case Level::None:    break;
    }
    return "?";
}

} // namespace

void installSink(SDK::Interface::ILogger& sink)
{
    sSink.store(&sink, std::memory_order_release);
}

uint32_t droppedCount()
{
    return sDropped.load(std::memory_order_relaxed);
}

Level runtimeLevel()
{
    return static_cast<Level>(sRuntimeLevel.load(std::memory_order_relaxed));
}

void setRuntimeLevel(Level level)
{
    sRuntimeLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

void message(Level level,
             const char* module,
             const char* func,
             int line,
             const char* fmt, ...)
{
    // Single load. Nothing can clear it, so the pointer cannot go stale between
    // the test and the call.
    SDK::Interface::ILogger* sink = sSink.load(std::memory_order_acquire);
    if (sink == nullptr) {
        sDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    va_list args;
    va_start(args, fmt);
    sink->mvprintf(tagOf(level), module != nullptr ? module : "?", func, line, fmt, args);
    va_end(args);
}

void hexdump(Level level,
             const char* module,
             const char* func,
             int line,
             const void* data,
             int len)
{
    if (data == nullptr || len <= 0) {
        return;
    }

    const uint8_t* p = static_cast<const uint8_t*>(data);

    // 8 for the offset, 16 * 3 for the bytes, 1 for the mid-row gap, 1 for NUL.
    // Sized so that append() below can never be asked to truncate; it clamps
    // regardless, because "safe by arithmetic accident" is how the previous
    // version was safe.
    char buf[8 + (16 * 3) + 1 + 1];

    for (int i = 0; i < len; i += 16) {
        size_t used = 0;

        // Append with an explicit clamp. snprintf returns the length it WOULD
        // have written, so advancing by it unclamped walks the cursor past the
        // end of the buffer and drives the remaining count negative.
        const auto append = [&](const char* f, auto value) {
            if (used >= sizeof(buf) - 1) {
                return;
            }
            const int n = snprintf(buf + used, sizeof(buf) - used, f, value);
            if (n <= 0) {
                return;
            }
            const size_t room = sizeof(buf) - 1 - used;
            used += (static_cast<size_t>(n) > room) ? room : static_cast<size_t>(n);
        };

        append("%04X:", i);

        for (int j = 0; j < 16 && (i + j) < len; ++j) {
            if (j == 8 && used < sizeof(buf) - 1) {
                buf[used++] = ' ';
                buf[used]   = '\0';
            }
            append(" %02X", p[i + j]);
        }

        message(level, module, func, line, "%s\n", buf);
    }
}

} // namespace SDK::Log
