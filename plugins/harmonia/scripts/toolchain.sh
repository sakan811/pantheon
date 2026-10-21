# Sourced by build.sh and build_tests.sh: locates Xcode's own clang and SDK.
#
# The default /usr/bin/clang can link against a broken Command Line Tools SDK, so everything is
# pointed at the full Xcode instead. Sets XCODE, TC (toolchain bin dir), SDK, CC and CXX.
XCODE="${XCODE_DEV_DIR:-$(xcode-select -p)}"
TC="$XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
SDK="$XCODE/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
[ -d "$SDK" ] || SDK="$(xcrun --show-sdk-path)"
CC="$TC/clang"
CXX="$TC/clang++"
[ -x "$CXX" ] || { CC=clang; CXX=clang++; }
