#!/bin/sh
# Builds Starship for Switch 2 (ounce) and makes the release archive
# build-switch2/starship-switch2.zip.
#
#   DEVKITPRO=/path/to/devkitpro sh switch2/build.sh
#
# Needs devkitPro (switch-dev, switch-portlibs) and ounce-devkit 20261114 or
# newer in $DEVKITPRO.
#
# The archive has Starship.nro, starship.o2r (the port's own assets, made
# without a ROM), config.yml and assets/yaml (the extractor's descriptions of
# the ROM) and the readme. sf64.o2r is made from a ROM, so it is never put in
# the archive: each player makes their own (see switch2/README.md).

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-switch2"

git -C "$ROOT" submodule update --init libultraship tools/Torch

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/switch2/cmake/Switch2.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$BUILD" --target Starship_nro

# Stop if the NRO is not marked as a Switch 2 program (old SDK or libnx).
python3 "$ROOT/switch2/check_nro.py" "$BUILD/Starship.nro"

# Start from an empty folder, so no file from an earlier build gets in.
DIST="$BUILD/dist"
rm -rf "$DIST" "$BUILD/starship-switch2.zip"
mkdir -p "$DIST/switch/starship"
cp "$BUILD/Starship.nro" "$DIST/switch/starship/"
cp "$ROOT/config.yml" "$DIST/switch/starship/"
cp -R "$ROOT/assets" "$DIST/switch/starship/"
if [ -f "$ROOT/starship.o2r" ]; then
    cp "$ROOT/starship.o2r" "$DIST/switch/starship/"
else
    echo "warning: starship.o2r not found; make it with the GeneratePortO2R target" >&2
fi
cp "$ROOT/switch2/README.md" "$DIST/readme.txt"
cp "$ROOT/switch2/GAME-DATA.md" "$DIST/GAME-DATA.md"

# Stop if a ROM-derived file got into the archive folder.
if find "$DIST" -name 'sf64*.o2r' -o -name '*.z64' -o -name '*.n64' -o -name '*.v64' | grep -q .; then
    echo "error: ROM-derived files found in $DIST" >&2
    exit 1
fi

(cd "$DIST" && zip -qr "$BUILD/starship-switch2.zip" .)
echo "Release archive: $BUILD/starship-switch2.zip"
unzip -l "$BUILD/starship-switch2.zip" | tail -3
