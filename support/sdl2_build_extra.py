#
# PlatformIO extra script for the native (SDL2) simulator build.
#
# The `native` platform has no notion of SDL2, so the include and link flags
# are queried from the host at build time instead of being hardcoded. Both
# `sdl2-config` and `pkg-config sdl2` are tried, in that order.
#
# In addition to the directory SDL2 reports (typically `<prefix>/include/SDL2`)
# its parent is added to the include path, so that both `#include <SDL.h>` and
# `#include <SDL2/SDL.h>` resolve.
#

import os
import subprocess

Import("env")

if env.get("PIOPLATFORM") != "native":
    # Nothing to do for the embedded targets.
    Return()


def query(*command):
    try:
        out = subprocess.check_output(command, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.decode("utf-8", "replace").split()


def sdl2_flags(kind):
    """Return the SDL2 flags of the given kind ("cflags" or "libs")."""
    flags = query("sdl2-config", "--" + kind)
    if flags is None:
        flags = query("pkg-config", "--" + kind, "sdl2")
    if flags is None:
        raise SystemExit(
            "\nSDL2 development files were not found.\n"
            "Neither `sdl2-config` nor `pkg-config sdl2` could be run.\n\n"
            "  Debian/Ubuntu: sudo apt install libsdl2-dev\n"
            "  Fedora:        sudo dnf install SDL2-devel\n"
            "  Arch:          sudo pacman -S sdl2\n"
            "  macOS:         brew install sdl2\n"
        )
    return flags


include_dirs = []
other_cflags = []

for flag in sdl2_flags("cflags"):
    if flag.startswith("-I"):
        path = flag[2:]
        include_dirs.append(path)
        # Allow the `SDL2/SDL.h` spelling as well as the bare `SDL.h` one.
        if os.path.basename(path) == "SDL2":
            include_dirs.append(os.path.dirname(path))
    else:
        other_cflags.append(flag)

env.Append(CPPPATH=include_dirs)
env.Append(CCFLAGS=other_cflags)

# SDL2 must come after the object files, hence LIBS/LINKFLAGS rather than CCFLAGS.
for flag in sdl2_flags("libs"):
    if flag.startswith("-l"):
        env.Append(LIBS=[flag[2:]])
    elif flag.startswith("-L"):
        env.Append(LIBPATH=[flag[2:]])
    else:
        env.Append(LINKFLAGS=[flag])

# The simulator uses a background thread for the LVGL tick.
env.Append(LIBS=["pthread"])
