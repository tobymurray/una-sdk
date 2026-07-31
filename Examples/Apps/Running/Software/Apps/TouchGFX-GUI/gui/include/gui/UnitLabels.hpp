#ifndef UNITLABELS_HPP
#define UNITLABELS_HPP

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/TypedText.hpp>

#include "SDK/Units/Units.hpp"

/**
 * @brief The one place this app's unit strings are named.
 *
 * TouchGFX generates TypedTextIds per app, so SDK::Units talks in abstract
 * Labels and each app maps them to its own text ids here. Everything else --
 * conversion, precision, when to show a placeholder -- lives in the SDK, and
 * no widget needs to know whether the user is on metric or imperial.
 */
namespace App
{

inline touchgfx::TypedTextId unitTextId(SDK::Units::Label label)
{
    switch (label) {
        case SDK::Units::Label::Mile:
            return T_TEXT_MI;
        case SDK::Units::Label::Metre:
            return T_TEXT_M;
        case SDK::Units::Label::Kilometre:
        default:
            // Running measures distance only, so km is the sole sensible
            // fallback; speed and elevation labels are added here if the app
            // ever grows those faces.
            return T_TEXT_KM;
    }
}

} // namespace App

#endif // UNITLABELS_HPP
