# Linux simulator: what upstream's page does not cover

Upstream `Docs/Simulator.md` § "Linux (GCC)" is the instruction set: dependencies,
the build command, the headless run, and an inventory of what makes Linux work
across `Libs/`, `ThirdParty/` and each app's `config/gcc/app.mk`. Read that first.

Everything this page once carried as "patches to apply to upstream" has since been
merged there, so only the following is left.

Tested on Ubuntu 24.04 / GCC 13 and Arch Linux / GCC 15.

## Arch packages

Upstream lists the Debian names only. The Arch equivalents:

```bash
sudo pacman -S base-devel ruby sdl2 sdl2_image libjpeg-turbo
gem install nokogiri
```

Nokogiri is the only gem the build needs. `textconvert` also reaches for `roo`,
but only through the `.xlsx` text-database parser, and every app in this repo
keeps its strings in `texts.xml`.

## Capturing a screenshot of a running simulator

The simulator opens one SDL2 window titled after the app folder. To capture it
from a non-interactive shell, with `xdotool` and ImageMagick installed:

```bash
DISPLAY=:0.0 ./build/bin/simulator.out &
sleep 1
WID=$(DISPLAY=:0.0 xdotool search --name "<AppName>" | head -1)
# if the app folder name does not match the window title:
# WID=$(DISPLAY=:0.0 xdotool search --pid $(pgrep -f simulator.out) | head -1)
DISPLAY=:0.0 import -window "$WID" /tmp/sim.png
```

`xdotool getwindowgeometry "$WID"` reports the window size, which comes from the
app's own `SimConstants.hpp`.

## The x86-64 host does not share the watch's struct layout

The message struct-size `static_assert`s in `MessageBase.hpp` and
`CommandMessages.hpp` are guarded to `#if __SIZEOF_POINTER__ == 4`, so on a
64-bit host they do not run. They encode the 32-bit ARM wire layout, and any
pointer-carrying struct really is larger under the simulator. Simulating is
unaffected; comparing a `sizeof` against the firmware's is not.
