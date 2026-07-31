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
        __attribute__((format(printf, 2, 3))) = 0;

    /**
     * @brief Log a formatted message.
     *
     * This method allows the application to log messages in a formatted way.
     *
     * @param format Format string (printf-style).
     * @param args Variable argument list
     */
    virtual void vprintf(const char *format, va_list args)
        __attribute__((format(printf, 2, 0))) = 0;

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
        __attribute__((format(printf, 6, 0))) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~ILogger() = default;

};

} // namespace SDK::Interface
