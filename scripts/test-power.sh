#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test "$(uname -s)" = Darwin || { echo 'These tests require macOS.' >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Isrc tests/power.cpp -framework CoreFoundation -o "$work/power-test"
"$work/power-test"
