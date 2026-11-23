# Starship for Switch 2

This folder builds Starship (the Star Fox 64 port) for Switch 2 with the ounce
homebrew SDK (libounce and OunceVK). It is listed in the homebrew menu as
"HarbourMasters - Starship".

To install and play it, see `PLAYER-README.txt` and `GAME-DATA.md`.

## Build from source

You need devkitPro with `switch-dev` and `switch-portlibs`, and ounce-devkit
20261117 or newer installed in `$DEVKITPRO` (`sh install.sh` from ounce-devkit) (it adds `libounce` and
`portlibs/switch2`). Older SDKs are refused: their NROs are not marked as
Switch 2 programs, and the console warns about them.

1. Make `starship.o2r` with a host (computer) build. It needs no ROM:

   ```sh
   cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build-cmake --target GeneratePortO2R
   ```

2. Build the NRO and the release archive:

   ```sh
   DEVKITPRO=/path/to/devkitpro sh switch2/build.sh
   ```

   This writes `build-switch2/starship-switch2.zip`. The script stops if the
   NRO is not a Switch 2 program, or if `sf64.o2r` or a ROM gets into the
   archive.

## Changes for Switch 2

- `switch2/cmake/Switch2.cmake`: the CMake toolchain. It loads the devkit's
  own `$DEVKITPRO/libounce/cmake/Switch2.cmake` (libounce, the Switch 2
  portlibs with EGL and GL on OunceVK through Zink first, `-D__SWITCH2__` and
  the Switch 2 CPU flags). `CMAKE_SYSTEM_NAME` stays `NintendoSwitch`, so the
  Switch code is used.
- `switch2/check_nro.py`: `build.sh` runs it on `Starship.nro` to check that
  libounce marked it as a Switch 2 program (the `OUN0` record).
- `CMakeLists.txt`: on Switch 2 the homebrew menu title is
  "HarbourMasters - Starship".
- libultraship (submodule, `starship-ounce` branch of
  <https://github.com/mysterypaintwo/libultraship-Switch2>): upstream
  libultraship has no Switch support, so the branch merges KiritoDv's
  `main-nx` Switch port into the commit that Starship uses. On Switch 2, the
  Switch 1 CPU clock table is not applied, the display size is 1920x1080, and
  OpenGL is a 4.1 core context with the core-profile shaders (as on macOS).
  The log has the OpenGL version and renderer.
