#!/usr/bin/env bash
# Builds PvZ Co-op and packages the zip you send to your friend.
#
#   ./build.sh          release build -> PvZ-Coop-dist.zip (what you share)
#   ./build.sh dev      quick dev build only -> build/PvZ-Coop.exe
#
# Or double-click BUILD.bat, which runs this.
#
# Nothing here launches the game or touches your save
# (%APPDATA%\io.github.wszqkzqk\PvZPortable).

set -euo pipefail

# The compiler lives in MSYS2 (UCRT64). Change this if it ever moves.
MSYS_BIN="/d/Programs/C/ucrt64/bin"
export PATH="$MSYS_BIN:$PATH"

cd "$(dirname "$0")"
ROOT="$(pwd)"

if ! command -v cmake >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1; then
    echo "Could not find cmake/g++ in $MSYS_BIN. Edit MSYS_BIN at the top of build.sh."
    exit 1
fi

# New or changed Adventure Plus art and sounds (assets/coop/<name>/ folders,
# newsoundsandsongs) are packed into the game first, whenever any is newer
# than the last pack. Needs Python (Pillow) and ffmpeg.
FFMPEG_BIN="/c/ffmpeg/bin"
if [ -n "$(find assets/coop -newer src/Lawn/CoopPackedData.cpp \( -name '*.png' -o -name '*.wav' -o -name '*.mp3' \)         -not -path '*/packed/*' -not -path '*/zombie-parts/*' -not -path '*/dave-parts/*' -not -path '*/headwear-originals/*' -print -quit)" ]; then
    echo "== Packing new art and sounds =="
    PATH="$FFMPEG_BIN:$PATH" python tools/zombie-art/pack_coop_assets.py
fi

if [ "${1:-}" = "dev" ]; then
    echo "== Dev build (build/) =="
    [ -f build/build.ninja ] || cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    echo
    echo "Done: build/PvZ-Coop.exe (needs MSYS2 on PATH to run; use the zip to share)."
    exit 0
fi

echo "== Release build (build-dist/) =="
# Static: one self-contained exe, no DLLs to ship. Console kept for the tests.
[ -f build-dist/build.ninja ] || cmake -G Ninja -B build-dist -DCMAKE_BUILD_TYPE=Release -DCONSOLE=ON -DBUILD_STATIC=ON
cmake --build build-dist

echo "== Packaging =="
PKG="build-dist/package/PvZ-Coop"
rm -rf build-dist/package
mkdir -p "$PKG"
cp build-dist/PvZ-Coop.exe "$PKG/"
strip "$PKG/PvZ-Coop.exe"
cp tools/windows/*.bat tools/windows/*.ps1 tools/windows/HOW-TO-PLAY.txt "$PKG/"
# Windows line endings for the scripts and the readme.
for f in "$PKG"/*.bat "$PKG"/*.ps1 "$PKG"/*.txt; do
    sed -i 's/\r$//; s/$/\r/' "$f"
done

echo "== Self-test (console only, no game window) =="
"$PKG/PvZ-Coop.exe" --coop-selftest | grep -E "selftest: (PASS|FAIL)"

echo "== Zipping =="
rm -f PvZ-Coop-dist.zip
if command -v python >/dev/null 2>&1; then
    python - <<'EOF'
import os, zipfile
root = 'build-dist/package'
with zipfile.ZipFile('PvZ-Coop-dist.zip', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in sorted(os.listdir(os.path.join(root, 'PvZ-Coop'))):
        z.write(os.path.join(root, 'PvZ-Coop', name), 'PvZ-Coop/' + name)
EOF
else
    powershell -NoProfile -Command "Compress-Archive -Path 'build-dist/package/PvZ-Coop' -DestinationPath 'PvZ-Coop-dist.zip'"
fi

echo
echo "Done: $ROOT/PvZ-Coop-dist.zip"
echo "Both players need this same zip."
