# Starship for Switch 2

This folder builds Starship (the Star Fox 64 port) for Switch 2 with the ounce
homebrew SDK (libounce and OunceVK). It is listed in the homebrew menu as
"HarbourMasters - Starship".

## Install

1. Copy the `switch/starship/` folder from the archive to the root of the SD
   card. It has `Starship.nro`, `starship.o2r` (the port's own assets),
   `config.yml` and `assets/` (used to read the ROM).
2. Give Starship the game assets (`GAME-DATA.md`, next to this file, explains
   it step by step):
   - Copy your own US ROM (`.z64`, `.n64` or `.v64`, any name) to
     `/switch/starship/`. Starship makes `sf64.o2r` from it on the first
     start. This takes a few minutes.
   - Or make `sf64.o2r` on a computer with the PC version of Starship, and
     copy it to `/switch/starship/`.
   - Optional: a JP or EU ROM in the same folder adds Japanese or European
     voices (Settings > Language > Install JP/EU Audio).
3. Start "HarbourMasters - Starship" from the homebrew menu.

The archive never has `sf64.o2r` or a ROM: they are made from a ROM, and you
must use your own.

Supported ROMs (SHA-1 of the `.z64` file):

- US 1.0: `d8b1088520f7c5f81433292a9258c1184afa1457`
- US 1.1: `09f0d105f476b00efa5303a3ebc42e60a7753b7a`

## Playing

- Press `-` to open the menu.
- Default buttons: B fires, Y drops a bomb, ZL boosts, ZR brakes, L and R
  roll, X looks first person (C-up), and A answers calls (C-right). The
  right stick is the C buttons. If you played an earlier build, your saved
  controls stay: reset them in the controller settings to get these.
- Quit: use "Starship > Quit to Homebrew Menu" in the menu to go back to the homebrew menu.
  Closing the game from the HOME menu also works.
- 120 FPS: on the Switch 2 screen, set the FPS slider in the graphics
  settings above 60. With vsync on, the game is held to the display's refresh
  rate (120 Hz handheld, 60 Hz on most TVs).
- Bug reports: the log is in `/switch/starship/logs/`. Send it with the report,
  especially after a crash: the folder then also has a `crash-<date>.txt`
  file that says where the game crashed.

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
