/**
 ******************************************************************************
 * @file    Config.hpp
 * @date    19-01-2024
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Configuration for GUI.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __GUI_CONFIG_HPP
#define __GUI_CONFIG_HPP

#include <cstdint>
#include <cstdbool>

#include "SDK/GUI/Button.hpp"
#include "SDK/GUI/Config.hpp"
#include <texts/TextKeysAndLanguages.hpp>

#define GUI_CONFIG_MS_2_TICKS(ms) ((ms)/(1000 / SDK::GUI::Config::kFrameRate))
namespace Gui
{

namespace Config
{

constexpr uint32_t kScreenTimeoutSteps = GUI_CONFIG_MS_2_TICKS(30000);     // 30s

} // namespace Config

} // namespace Gui

#endif /* __GUI_CONFIG_HPP */
