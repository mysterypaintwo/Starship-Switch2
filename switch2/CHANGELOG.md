# Changelog

## v1.1

**Added**
- A full-screen menu with search, sized for the Switch 2 screen.
- One Voices menu for English, Japanese and European (Lylat Wars).
- A loading screen with a progress bar.

**Improved**
- Much less stutter. Shaders are prepared during boot and game data stays in memory.
- JP and EU audio install together from one button, with a warning before the screen freezes.
- The install checks for 64 MB of free space. If the SD card fills up, it cancels and removes its files.
- The quit option is now called Quit Game.

**Fixed**
- The crash when closing the game.
- The crashes when installing or loading JP and EU audio.
- Audio crackling and playback problems.
- Holding Y no longer opens a window switcher.
- Slider + buttons are no longer cut off.
- Full volume now shows as 100%, and volume settings no longer drop by one after a restart.
- Missing game files now show a clear message. Press A to return to the Homebrew Menu.

**Updating:** replace the files in `/switch/starship/` and keep your `sf64.o2r`. The first launch takes longer while it prepares shaders.

## v1.0 Pre-Release

Initial release.
