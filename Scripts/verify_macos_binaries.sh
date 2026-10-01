#!/bin/bash
# Verify the built macOS plugin binaries: every one must be a fat arm64 + x86_64 Mach-O and
# declare a minimum macOS of exactly 11.0 on BOTH slices.
#
#   Scripts/verify_macos_binaries.sh [BUILD_DIR]      (BUILD_DIR defaults to ./build)
#
# Checked: the VST3, AU and Standalone executables. The Standalone path is overridden via
# RUNTIME_OUTPUT_DIRECTORY in CMakeLists.txt, so it lands under BUILD_DIR/bin/Standalone.
#
# A deployment target below 10.14 makes the x86_64 slice carry LC_VERSION_MIN_MACOSX, which has
# no "minos" line; reading minos alone would miss it, so that load command fails the check too.

set -u

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${1:-$(dirname "$SCRIPT_DIR")/build}"
EXPECTED_MINOS="11.0"

BINARIES=(
    "$BUILD_DIR/Unravel_artefacts/Release/VST3/Unravel.vst3/Contents/MacOS/Unravel"
    "$BUILD_DIR/Unravel_artefacts/Release/AU/Unravel.component/Contents/MacOS/Unravel"
    "$BUILD_DIR/bin/Standalone/Unravel.app/Contents/MacOS/Unravel"
)

status=0
fail() { echo "ERROR: $*"; status=1; }

for bin in "${BINARIES[@]}"; do
    if [ ! -f "$bin" ]; then
        fail "missing binary: $bin"
        continue
    fi

    archs="$(lipo -archs "$bin")"
    echo "$bin: archs [$archs]"
    for arch in arm64 x86_64; do
        case " $archs " in
            *" $arch "*) ;;
            *) fail "$bin has no $arch slice"; continue ;;
        esac

        # otool -arch selects one slice of the fat binary.
        load_cmds="$(otool -arch "$arch" -l "$bin")"
        minos="$(printf '%s\n' "$load_cmds" | awk '/^ *minos /{print $2}' | sort -u | tr '\n' ' ' | sed 's/ *$//')"
        echo "  $arch: minos [$minos]"
        if [ "$minos" != "$EXPECTED_MINOS" ]; then
            fail "$bin ($arch) minos is [$minos], expected exactly $EXPECTED_MINOS"
        fi
        if printf '%s\n' "$load_cmds" | grep -q LC_VERSION_MIN_MACOSX; then
            fail "$bin ($arch) carries LC_VERSION_MIN_MACOSX (a pre-10.14 deployment target)"
        fi
    done
done

if [ "$status" -eq 0 ]; then
    echo "OK: all binaries are arm64 + x86_64 with minos $EXPECTED_MINOS on both slices"
fi
exit "$status"
