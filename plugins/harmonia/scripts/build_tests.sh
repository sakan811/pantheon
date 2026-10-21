#!/bin/zsh
# Builds the offline DSP tools with Xcode's toolchain (no JUCE required) and runs the accuracy tests.
#   scripts/build_tests.sh          build + run tests
# Also produces build-tools/tune_wav, a command-line tuner for WAV files:
#   build-tools/tune_wav in.wav out.wav --key C --scale major --speed 40 --natural 0.7
set -e
cd "$(dirname "$0")/.."
XCODE="${XCODE_DEV_DIR:-$(xcode-select -p)}"
SDK="$XCODE/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
CXX="$XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
[ -x "$CXX" ] || CXX=clang++
[ -d "$SDK" ] || SDK="$(xcrun --show-sdk-path)"
mkdir -p build-tools
"$CXX" -isysroot "$SDK" -std=c++17 -O2 -Wall -Wextra Tests/tune_wav.cpp -o build-tools/tune_wav
"$CXX" -isysroot "$SDK" -std=c++17 -O2 -Wall -Wextra Tests/dsp_test.cpp -o build-tools/dsp_test
./build-tools/dsp_test
