# Make the game data for Starship (Switch 2)

Starship needs `sf64.o2r`, made from your own Star Fox 64 ROM. This file holds
the game's data, so it is not in the download. **The Switch 2 makes it
itself:** you only copy your ROM to the SD card. No computer program is
needed.

## What you need

- A Star Fox 64 ROM that you dumped yourself:

  | ROM | SHA-1 (`.z64`) | Use |
  |---|---|---|
  | US 1.0 | `d8b1088520f7c5f81433292a9258c1184afa1457` | the game |
  | US 1.1 | `09f0d105f476b00efa5303a3ebc42e60a7753b7a` | the game |
  | JP 1.0 | `9bd71afbecf4d0a43146e4e7a893395e19bf3220` | Japanese voices (optional) |
  | EU 1.0 | `05b307b8804f992af1a1e2fbafbd588501fdf799` | European voices (optional) |

  Any file name works, with the extension `.z64`, `.n64` or `.v64` (all three
  byte orders work; a `.n64` or `.v64` file has another SHA-1 than the table).

## Steps

1. Copy your US ROM to `/switch/starship/` on the SD card, next to
   `Starship.nro`.
2. Start Starship from the homebrew menu and answer **Yes** to "Asset
   Extraction". The Switch 2 makes `sf64.o2r`. This takes a few minutes.
3. Starship then asks "Extract another?":
   - **No**: play with the US voices.
   - **Yes**: only if a JP or EU ROM is already in `/switch/starship/`.
4. You can delete the ROM from the SD card once `sf64.o2r` is made.

## Japanese or European voices (optional)

English is already in your US game. Japanese needs the JP ROM, and European
(Lylat Wars) needs the EU ROM.

1. Copy your JP and/or EU ROM to `/switch/starship/`. You need 64 MB free on
   the SD card.
2. In Starship, open the menu (`-`), then Settings > Language > **Install
   JP/EU Audio**. The screen freezes while it installs. Do not close the game.
3. Pick the language under **Voices**, then restart Starship.

You can delete the ROMs from the SD card afterward.

## Made on a computer instead

A `sf64.o2r` made by the PC version of Starship (2.0.0, from
<https://github.com/HarbourMasters/Starship/releases>) probably works too, but
making it on the Switch 2 is the tested way. Copy it to `/switch/starship/`.

## If it does not start

- "No supported US ROM found": the ROM is not in `/switch/starship/`, or it is
  not one of the versions above.
- Nothing happens or the game closes: send `/switch/starship/logs/` from the
  SD card with your report. After a crash, the folder also has a
  `crash-<date>.txt` file.
