#!/bin/sh
# Runs the Astralay test program on a headless Linux box.
#
# JUCE's Linux backend looks up WM_PROTOCOLS, WM_TAKE_FOCUS and friends with
# onlyIfExists=True and writes whatever it gets into the first window it realises.
# On an X server that has never had a window manager — a plain `xvfb-run`, a CI
# container — those atoms do not exist, and the zero atom lands as BadAtom, which
# Xlib's default error handler treats as fatal. intern-x-atoms creates them first;
# see scripts/linux/intern-x-atoms.c.
#
# Usage: scripts/linux/run-tests.sh [build-dir] [configuration]
# Needs: Xvfb (the xvfb package) and a C compiler for the helper.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${1:-$root/build}
config=${2:-Release}
# Extra arguments go to the test program itself (for example --benchmark).
if [ $# -ge 1 ]; then shift; fi
if [ $# -ge 1 ]; then shift; fi

helper="$build/intern-x-atoms"
cc=${CC:-cc}
if [ ! -x "$helper" ] || [ "$root/scripts/linux/intern-x-atoms.c" -nt "$helper" ]; then
    "$cc" -O1 -o "$helper" "$root/scripts/linux/intern-x-atoms.c" -lX11
fi

# intern-x-atoms holds the WM atoms for as long as it is connected (see the comment
# in the C file), so it must still be running while the tests create their windows;
# trap kills it when the inner shell exits, whatever the test result.
exec xvfb-run -a sh -c "'$helper' & holder=\$!; trap 'kill \"\$holder\" 2>/dev/null' EXIT; '$build/AstralayTests_artefacts/$config/AstralayTests' \"\$@\"" sh "$@"
