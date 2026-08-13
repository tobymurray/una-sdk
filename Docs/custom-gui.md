(custom-gui)=
# Custom GUI (drawing without TouchGFX)

A GUI process does not have to be a TouchGFX application. The SDK ships a second
GUI entry point, **CustomGUI**, for apps that render into the framebuffer
themselves and push it to the kernel. The kernel's display contract is two
messages wide, so what fills the buffer is the app's choice.

Use it when an app wants a renderer TouchGFX does not provide, when the GUI is
simple enough that a widget framework is overhead, or when the app must stay clear
of TouchGFX's licence terms (see [THIRD-PARTY-LICENSES.md](../THIRD-PARTY-LICENSES.md)).

Use TouchGFX when you want its widget library, font pipeline, and Designer
tooling. See [TouchGFX Port Architecture](TouchGFX-Port-Architecture.md).

## Three ways an app puts pixels on screen

| Path | GUI process | Who draws |
|---|---|---|
| TouchGFX | yes | app, via the framework and the SDK's port |
| CustomGUI | yes | app, straight into the framebuffer |
| Glance | no | kernel, from a list of controls the service sends |

A glance is not a cut-down GUI process. It has no GUI ELF at all: the service
asks the kernel for the glance geometry with `RequestGlanceConfig`, sends
`SDK::Glance` controls (text, image, line, rect) with `RequestGlanceUpdate`, and
the kernel renders them in the 240x60 glance area. That is why `app_merging.py`
treats the packed GUI half as optional for type `Glance` and required for every
other type.

## The entry point contract

`Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp` asks for one thing: a class
named `Gui`, declared in a header named `Gui.hpp` that is reachable on the include
path by bare name.

```cpp
class Gui {
public:
    explicit Gui(SDK::Kernel &kernel);
    void run();   // blocks; returning exits the GUI process
};
```

The entry point builds the kernel wrapper, registers it through
`SDK::KernelProviderGUI`, starts the logger, placement-news `Gui` into static
storage, and calls `run()`. No framework loop sits underneath: `run()` owns the
kernel message queue for the life of the process.

## Display contract

Ask for the geometry once, then push whole frames.

**`RequestDisplayConfig`** is request/response. The kernel fills `width`,
`height`, and `colorDepth`. The `format` field is reserved and unused; the
framebuffer is **ABGR2222**, 8 bits per pixel, 2 bits each for alpha, blue, green
and red. Clamp the returned size against what your framebuffer can hold before
rendering into it.

**`RequestDisplayUpdate`** takes `pBuffer`, pointing at a `width * height` byte
buffer. Its `x`, `y`, `width` and `height` fields are reserved and not used: the
kernel always updates the entire buffer, so there is no partial-region path to
optimise for. At 240x240 that is one 57,600 byte frame per update.

The buffer must stay valid until the response arrives. A `static` buffer in the
GUI process guarantees that.

Because every update is a full frame, redrawing everything each tick matches this
contract directly. Retained-mode invalidation buys nothing at this boundary.

## Messages the loop must handle

| Message | Meaning |
|---|---|
| `EVENT_GUI_TICK` | frame pacing. Ack and release it, then render and push. |
| `COMMAND_APP_GUI_RESUME` | the GUI is visible, so start painting. |
| `COMMAND_APP_GUI_SUSPEND` | the GUI is hidden, so stop until resumed. |
| `COMMAND_APP_STOP` | ack, release, then `kernel.sys.exit(0)`, which does not return on device. |
| `EVENT_BUTTON` | `Id::SW1` to `SW4` with `Event::PRESS`, `RELEASE`, `CLICK`, `LONG_PRESS`, `HOLD_1S`, `HOLD_5S`, `HOLD_10S`. |

Two things are easy to get wrong:

- **Paint only while resumed.** Pushing frames while suspended fights whatever
  else owns the screen.
- **Handle back.** `Id::SW4` (bottom right, R2) is back by convention. This loop
  receives every button event and nothing else will act on them, so an app with no
  back handling cannot be left except by rebooting the watch.

`Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp` is the reference for how
the TouchGFX port services these same messages.

## Assets

`Utilities/Scripts/png2abgr2222/png2abgr2222.py` converts PNGs to ABGR2222 byte
arrays in a C header, with `WIDTH`, `HEIGHT` and `SIZE` macros per icon. It is
independent of TouchGFX and of this entry point, so blit the arrays into your
framebuffer however you like.

## CMake wiring

`UNA_SDK_SOURCES_GUI_CUSTOM` is the CustomGUI counterpart to
`UNA_SDK_SOURCES_GUI`. Such an app needs no TouchGFX include directories:

```cmake
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)

# This app's GUI source root. Feeds -fmacro-prefix-map, so the build stays
# reproducible. TOUCHGFX_PATH is the older alias for the same thing.
set(GUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CustomGUI")

set(GUI_SOURCES
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_GUI_CUSTOM}
    ${GUI_PATH}/Gui.cpp
)
set(GUI_INCLUDE_DIRS
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${GUI_PATH}            # Gui.hpp is included by bare name
)

# Optional: archives to link into the GUI process, such as a rendering library or
# a staticlib from another language. TOUCHGFX_LIBS is the alias.
# set(GUI_LIBS ${MY_RENDER_LIB})

una_app_build_gui(${APP_NAME}GUI.elf)
una_app_build_app()          # merges the GUI ELF because the target exists
```

`una_app_build_gui()` needs no TouchGFX: with neither libs variable set, the link
group is just `-l:libstdc++.a`. The linker script, stack and RAM defsyms, and
packaging step are the same either way. `una_app_build_app()` merges a GUI ELF
whenever `una_app_build_gui()` created its target, so nothing has to define
`TOUCHGFX_PATH` to get one merged.

## What the app takes on

TouchGFX supplies more than a HAL, and a CustomGUI app supplies its own
replacements for all of it:

- text and fonts, meaning glyph data, kerning, unicode, and value formatting
- bitmaps, meaning decoding, scaling and blitting
- widgets and screens, including the screen model
- animation and transitions
- layout tooling, since there is no Designer for this path

## Simulator

The desktop simulator does not support this path yet. Its frame loop and mocks
reach into the TouchGFX port: `Libs/Source/Simulator/App/AppCore.cpp` and
`Libs/Source/Simulator/Kernel/Kernel.cpp` drive
`SDK::TouchGFXCommandProcessor`, `Libs/Source/Simulator/Kernel/Mock/System.cpp`
calls `touchgfx::HALSDL2::stopApplication()`, and
`Libs/Header/SDK/Simulator/Kernel/Mock/Logger.hpp` uses `touchgfx_printf`. So a
CustomGUI app has to be verified on hardware, or against a renderer whose drawing
code can also be built and previewed on the host on its own terms.
