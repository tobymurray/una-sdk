/**
 ******************************************************************************
 * @file    Logger.h
 * @date    04-10-2025
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Logger component for application.
 ******************************************************************************
 *
 * Usage example:
 *
 * file: MyFile.cpp
 * @code{.cpp}
 *
 * #define LOG_MODULE_PRX    "MyPrefix"
 * #define LOG_MODULE_LEVEL  LOG_LEVEL_DEBUG
 * #include "SDK/UnaLogger/Logger.h"
 *
 * void MyModule::function()
 * {
 *     LOG_INFO("some text\n");
 *
 *     uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
 *     LOG_DEBUG_DUMP(data, sizeof(data));
 * }
 *
 * @endcode
 *
 ******************************************************************************
 *
 * DESIGN NOTES -- read before changing anything in this header.
 *
 * Level filtering is done with `if constexpr` at the CALL SITE, never with `#if`
 * around a declaration or an entity body. That single choice is what makes the
 * following four properties hold simultaneously; breaking it breaks all four.
 *
 *  1. Zero code is emitted for a filtered-out call, at any optimisation level.
 *     The discarded branch of an `if constexpr` is not codegen'd.
 *
 *  2. A filtered-out call is still PARSED AND TYPE-CHECKED. Outside a template
 *     both substatements of an `if constexpr` must be well-formed, so -Wformat
 *     still validates every LOG_* call even in a no-log build. The old
 *     `#define LOG(...) do {} while (0)` threw the arguments away unparsed,
 *     which is how format bugs survived at low levels.
 *
 *  3. No declaration is ever conditional, so there is no configuration in which
 *     a name is missing, and no way for two translation units to compile
 *     different bodies for the same inline entity (an ODR violation the linker
 *     resolves arbitrarily and silently).
 *
 *  4. LOG_MODULE_PRX and LOG_MODULE_LEVEL are expanded WHERE THEY ARE USED, not
 *     where this header is included. A translation unit therefore configures
 *     itself correctly even if this header was already pulled in transitively
 *     before the #defines -- which, with an include guard, silently discarded
 *     the per-TU level under the old `#if` scheme.
 *
 * The LOG_LEVEL_* names stay integer macros because existing sources test them
 * in real preprocessor conditionals (e.g. `#if LOG_MODULE_LEVEL ==
 * LOG_LEVEL_DEBUG` to compile out a diagnostic struct). Turning them into C++
 * constants would make those `#if`s silently compare 0 == 0.
 *
 * SDK::Interface::ILogger is deliberately NOT changed: it is the ABI boundary
 * with the host firmware, which supplies the sink through gIKernel. The Level
 * enum to legacy "D"/"I"/"W"/"E" mapping happens library-side, in Logger.cpp.
 *
 ******************************************************************************
 */

#ifndef __LOGGER_H
#define __LOGGER_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "SDK/Interfaces/ILogger.hpp"

// Log level. Integer macros: callers use these in real #if conditionals.
#define LOG_LEVEL_NO_LOG        0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARNING       2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4

/**
 * @brief Build-wide compile-time ceiling.
 *
 * This is the only place LOG_LEVEL is consulted, and it is consulted to pick the
 * value of a constexpr -- not to hide a declaration. A build with -DLOG_LEVEL=0
 * therefore compiles every source file in the tree, including this component's
 * own implementation, and simply emits no logging code.
 */
#if !defined(LOG_LEVEL)
#define LOG_LEVEL               4
#endif

namespace SDK::Log
{

/**
 * @brief Severity. A numerically lower value is more severe.
 */
enum class Level : uint8_t {
    None    = LOG_LEVEL_NO_LOG,
    Error   = LOG_LEVEL_ERROR,
    Warning = LOG_LEVEL_WARNING,
    Info    = LOG_LEVEL_INFO,
    Debug   = LOG_LEVEL_DEBUG,
};

/**
 * @brief Compile-time ceiling as a typed constant.
 */
inline constexpr Level kCompileLevel = static_cast<Level>(LOG_LEVEL);

/**
 * @brief Strip directories from a path at compile time.
 *
 * Replaces the old __FILENAME__, which ran strrchr() on __FILE__ on every log
 * call and embedded the full build path in the image.
 */
constexpr const char* basename(const char* path)
{
    const char* last = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return last;
}

// ---------------------------------------------------------------------------
// Sink installation
// ---------------------------------------------------------------------------

/**
 * @brief Install the sink that receives every log message.
 *
 * @param sink  Sink to install. MUST remain valid until the process ends: this
 *              component keeps a pointer to it and never clears it.
 *
 * The name states the contract, because the contract is the whole bug class.
 * There is deliberately no matching uninstall function. A sink that can be
 * uninstalled is a sink that can be uninstalled too late, and "clear the pointer
 * at exactly the right moment" is not an invariant a C++ program can enforce --
 * teardown order, background threads and static destructors all get a vote.
 * Outliving everything removes the question instead of answering it.
 *
 * @note Safe to call at any time from any thread; a later call replaces the
 *       sink. Publication is atomic.
 */
void installSink(SDK::Interface::ILogger& sink);

/**
 * @brief Number of messages dropped because no sink was installed yet.
 *
 * Exists so tests and start-up self-checks can assert nothing was silently
 * swallowed. Silence is the failure mode that hides logger bugs.
 */
uint32_t droppedCount();

// ---------------------------------------------------------------------------
// Runtime level
// ---------------------------------------------------------------------------

/**
 * @brief Current runtime ceiling. Defaults to the compile-time ceiling.
 */
Level runtimeLevel();

/**
 * @brief Change the runtime ceiling.
 *
 * Raising it above kCompileLevel has no effect: calls above the compile-time
 * ceiling emit no code, so there is nothing to re-enable.
 */
void setRuntimeLevel(Level level);

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

/**
 * @brief Format and emit one message. Call through the LOG_* macros.
 *
 * @param level   Severity.
 * @param module  Module name, or nullptr for "?".
 * @param func    Function name, typically __func__.
 * @param line    Line number, typically __LINE__.
 * @param fmt     printf-style format string.
 *
 * The format attribute is why this declaration has this shape: it makes every
 * LOG_* call site format-checked by the compiler, at every level.
 */
void message(Level level,
             const char* module,
             const char* func,
             int line,
             const char* fmt, ...) __attribute__((format(printf, 5, 6)));

/**
 * @brief Emit a hex dump. Call through the LOG_*_DUMP macros.
 */
void hexdump(Level level,
             const char* module,
             const char* func,
             int line,
             const void* data,
             int len);

/**
 * @brief Whether @p level passes the compile-time ceilings.
 *
 * Both parameters are int so this works whether a caller wrote
 * `#define LOG_MODULE_LEVEL LOG_LEVEL_INFO` or `#define LOG_MODULE_LEVEL 3`.
 */
constexpr bool compiledIn(int level, int moduleCeiling)
{
    return level <= LOG_LEVEL && level <= moduleCeiling;
}

/**
 * @brief Whether @p level passes the runtime ceiling.
 */
inline bool enabled(Level level)
{
    return static_cast<uint8_t>(level) <= static_cast<uint8_t>(runtimeLevel());
}

} // namespace SDK::Log

// ---------------------------------------------------------------------------
// Per-translation-unit configuration
// ---------------------------------------------------------------------------
//
// Both are expanded at the USE site by the macros below, never in a preprocessor
// conditional here, so a transitive include of this header cannot discard them.

#ifndef LOG_MODULE_PRX
#define LOG_MODULE_PRX          ::SDK::Log::basename(__FILE__)
#endif

#ifndef LOG_MODULE_LEVEL
#define LOG_MODULE_LEVEL        LOG_LEVEL_DEBUG
#endif

// ---------------------------------------------------------------------------
// The gate
// ---------------------------------------------------------------------------

/**
 * @brief Emit at @p LEVEL_ENUM if @p LEVEL_INT passes every ceiling.
 *
 * @param LEVEL_INT   LOG_LEVEL_* integer, for the constexpr comparison.
 * @param LEVEL_ENUM  Matching SDK::Log::Level, passed to the dispatcher.
 *
 * Wrapped in `if constexpr`, so a filtered-out call is type-checked but not
 * emitted. Do not turn this back into a `#if`. See the design notes above.
 */
#define UNA_LOG_AT(LEVEL_INT, LEVEL_ENUM, FMT, ...)                              \
    do {                                                                         \
        if constexpr (::SDK::Log::compiledIn((LEVEL_INT), (LOG_MODULE_LEVEL))) { \
            if (::SDK::Log::enabled(LEVEL_ENUM)) {                               \
                ::SDK::Log::message((LEVEL_ENUM), LOG_MODULE_PRX,                \
                                    __func__, __LINE__, FMT, ##__VA_ARGS__);     \
            }                                                                    \
        }                                                                        \
    } while (false)

/**
 * @brief Hex-dump counterpart of UNA_LOG_AT.
 */
#define UNA_LOG_DUMP_AT(LEVEL_INT, LEVEL_ENUM, DATA, LEN)                        \
    do {                                                                         \
        if constexpr (::SDK::Log::compiledIn((LEVEL_INT), (LOG_MODULE_LEVEL))) { \
            if (::SDK::Log::enabled(LEVEL_ENUM)) {                               \
                ::SDK::Log::hexdump((LEVEL_ENUM), LOG_MODULE_PRX,                \
                                    __func__, __LINE__, (DATA), (LEN));          \
            }                                                                    \
        }                                                                        \
    } while (false)

// ---------------------------------------------------------------------------
// User macros
// ---------------------------------------------------------------------------

/** @brief Error level logging macro. */
#define LOG_ERROR(FMT, ...) \
        UNA_LOG_AT(LOG_LEVEL_ERROR, ::SDK::Log::Level::Error, FMT, ##__VA_ARGS__)
/** @brief Error level hexdump macro. */
#define LOG_ERROR_DUMP(DATA, LEN) \
        UNA_LOG_DUMP_AT(LOG_LEVEL_ERROR, ::SDK::Log::Level::Error, DATA, LEN)

/** @brief Warning level logging macro. */
#define LOG_WARNING(FMT, ...) \
        UNA_LOG_AT(LOG_LEVEL_WARNING, ::SDK::Log::Level::Warning, FMT, ##__VA_ARGS__)
/** @brief Warning level hexdump macro. */
#define LOG_WARNING_DUMP(DATA, LEN) \
        UNA_LOG_DUMP_AT(LOG_LEVEL_WARNING, ::SDK::Log::Level::Warning, DATA, LEN)

/** @brief Info level logging macro. */
#define LOG_INFO(FMT, ...) \
        UNA_LOG_AT(LOG_LEVEL_INFO, ::SDK::Log::Level::Info, FMT, ##__VA_ARGS__)
/** @brief Info level hexdump macro. */
#define LOG_INFO_DUMP(DATA, LEN) \
        UNA_LOG_DUMP_AT(LOG_LEVEL_INFO, ::SDK::Log::Level::Info, DATA, LEN)

/** @brief Debug level logging macro. */
#define LOG_DEBUG(FMT, ...) \
        UNA_LOG_AT(LOG_LEVEL_DEBUG, ::SDK::Log::Level::Debug, FMT, ##__VA_ARGS__)
/** @brief Debug level hexdump macro. */
#define LOG_DEBUG_DUMP(DATA, LEN) \
        UNA_LOG_DUMP_AT(LOG_LEVEL_DEBUG, ::SDK::Log::Level::Debug, DATA, LEN)

// ---------------------------------------------------------------------------
// Compatibility
// ---------------------------------------------------------------------------

/**
 * @brief Legacy spelling of SDK::Log::installSink().
 *
 * Kept so the 19 existing entry points need no edit. The old name did not say
 * that the sink must outlive the process, which is a large part of why nobody
 * noticed that a kernel-owned sink violated it.
 */
inline void Logger_init(SDK::Interface::ILogger& ilogger)
{
    ::SDK::Log::installSink(ilogger);
}

#endif // __LOGGER_H
