#!/bin/bash
# Compile Swell Stutter (VST3 + AU + Standalone, binaire universel) et signe en ad hoc.
set -e
cd "$(dirname "$0")/.."
command -v cmake >/dev/null || { echo "CMake manquant : brew install cmake"; exit 1; }
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target SwellStutter_VST3 SwellStutter_AU SwellStutter_Standalone -j "$(sysctl -n hw.ncpu)"
A=build-release/SwellStutter_artefacts/Release
find "$A" -name '._*' -delete
for b in "$A/VST3/Swell Stutter.vst3" "$A/AU/Swell Stutter.component" "$A/Standalone/Swell Stutter.app"; do
    xattr -cr "$b"; codesign --force --deep -s - "$b"
done
echo "OK : $A"
