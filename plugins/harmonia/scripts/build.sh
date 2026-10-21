#!/bin/zsh
# Configure + build the Harmonia plug-ins (VST3, AU, Standalone) and install them for the current user.
#
# Uses Xcode's clang + SDK directly with Unix Makefiles, so it works even when `xcodebuild`
# itself cannot start (e.g. Xcode first-launch tasks were never run).
#
#   scripts/build.sh            # Release build
#   scripts/build.sh Debug
set -e
cd "$(dirname "$0")/.."

CONFIG="${1:-Release}"
XCODE="${XCODE_DEV_DIR:-$(xcode-select -p)}"
TC="$XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
SDK="$XCODE/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

# Make every sub-process (including JUCE's own helper builds) use Xcode's SDK.
export DEVELOPER_DIR="$XCODE"
export SDKROOT="$SDK"

cmake -S . -B build -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_C_COMPILER="$TC/clang" -DCMAKE_CXX_COMPILER="$TC/clang++" \
    -DCMAKE_OBJC_COMPILER="$TC/clang" -DCMAKE_OBJCXX_COMPILER="$TC/clang++" \
    -DCMAKE_OSX_SYSROOT="$SDK"

cmake --build build --config "$CONFIG" -j "$(sysctl -n hw.ncpu)"

# Ad-hoc sign everything we produced (required for Apple Silicon to load the plug-ins).
for bundle in \
    "$HOME/Library/Audio/Plug-Ins/VST3/Harmonia.vst3" \
    "$HOME/Library/Audio/Plug-Ins/Components/Harmonia.component"; do
    [ -d "$bundle" ] && codesign --force --deep --sign - "$bundle" && echo "signed $bundle"
done
echo "Done. Restart FL Studio (or rescan plug-ins) to pick up Harmonia."
