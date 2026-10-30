#!/bin/zsh
# Builds the offline DSP tools with Xcode's toolchain (no JUCE required) and runs the accuracy tests.
#   scripts/build_tests.sh          build + run tests
# Also produces build-tools/tune_wav, a command-line tuner for WAV files:
#   build-tools/tune_wav in.wav out.wav --key C --scale major --speed 40 --natural 0.7
set -e
cd "$(dirname "$0")/.."
source scripts/toolchain.sh
mkdir -p build-tools
for tool in tune_wav dsp_test pitch_track stress_corrector; do
    "$CXX" -isysroot "$SDK" -std=c++17 -O2 -Wall -Wextra "Tests/$tool.cpp" -o "build-tools/$tool"
done
./build-tools/dsp_test
