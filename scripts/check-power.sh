#!/bin/sh
# Print a battery reading with the same parser as the monitor. No sudo.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
"${CXX:-c++}" -std=c++17 -Isrc scripts/check-power.cpp -framework CoreFoundation -framework IOKit -o "$work/check-power"
"$work/check-power"
