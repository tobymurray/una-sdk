/**
 ******************************************************************************
 * @file    ILogger.hpp
 * @date    06-12-2025
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Interface for logging operations.
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <cstdarg>

/**
 * @def UNA_PRINTF_FMT
 * @brief Ask the compiler to check printf-style arguments against the format.
 *
 * @param FMT_IDX   1-based index of the format-string parameter. For a
 *                  non-static member function @c this is parameter 1, so the
 *                  first declared parameter is 2.
 * @param ARGS_IDX  1-based index of the first variadic argument, or 0 for a
 *                  function that takes a @c va_list instead of @c ... .
 *
 * Purely diagnostic: it carries no ABI implication, so it is safe on the
 * firmware-facing interface below.
 *
 * @c __attribute__ is GNU syntax. The Windows simulator builds with MSVC
 * (PlatformToolset v143 in every simulator/msvs/Application.vcxproj), which does
 * not implement it, so the annotation has to degrade to nothing there rather
 * than break every translation unit that includes this header. MSVC's nearest
 * equivalent is the SAL annotation @c _Printf_format_string_ on the format
 * parameter itself; it is a different shape and only checks under @c /analyze,
 * so it is deliberately not wired up here.
 *
 * Define @c UNA_PRINTF_FMT_OFF to force the unannotated path. That exists so the
 * non-GNU configuration can be compiled and tested on a GNU toolchain, rather
 * than only discovered on a machine nobody in CI has.
 */
#if defined(UNA_PRINTF_FMT_OFF) || !(defined(__GNUC__) || defined(__clang__))
#  define UNA_PRINTF_FMT(FMT_IDX, ARGS_IDX)
#else
// Kept on one line so a build check can assert that GNU attribute syntax appears
// nowhere else in the public headers.
#  define UNA_PRINTF_FMT(FMT_IDX, ARGS_IDX) __attribute__((format(printf, FMT_IDX, ARGS_IDX)))
#endif


namespace SDK::Interface
{

/**
 * @brief   Logger interface.
 */
class ILogger {
public:

    /**
     * @brief Log a formatted message.
     *
     * This method allows the application to log messages in a formatted way.
     *
     * @param format Format string (printf-style).
     * @param ... Additional arguments.
     */
    virtual void printf(const char *format, ...)
        UNA_PRINTF_FMT(2, 3) = 0;

    /**
     * @brief Log a formatted message.
     *
     * This method allows the application to log messages in a formatted way.
     *
     * @param format Format string (printf-style).
     * @param args Variable argument list
     */
    virtual void vprintf(const char *format, va_list args)
        UNA_PRINTF_FMT(2, 0) = 0;

    /**
     * @brief Log a formatted message with metadata.
     *
     * This method allows the application to log messages in a formatted way.
     * @param level  Log level string (e.g., "D", "I", "W", "E")
     * @param module_name Module name, file name or other identifier
     * @param func Function name (typically __func__)
     * @param line Line number (typically __LINE__)
     * @param fmt Format string (printf-style).
     * @param args Variable argument list
     */
    // The format attributes on these three are diagnostic only -- they do not
    // affect the ABI, so they are safe to add to this firmware-facing interface,
    // and they make every call through it format-checked by the compiler.
    virtual void mvprintf(const char *level, const char *module_name,
            const char *func, int line, const char *fmt, va_list args)
        UNA_PRINTF_FMT(6, 0) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~ILogger() = default;

};

} // namespace SDK::Interface
