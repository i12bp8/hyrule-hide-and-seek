#!/bin/sh
# Builds and runs the rules test. Needs a C++20 compiler with <format> (GCC 13+, Clang 17+).
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/hs_match_test"
${CXX:-c++} -std=c++20 -O1 -g -Wall -Wextra -DHS_TEST_CLOCK \
    -Itests/shim -Itests -Isrc \
    src/match.cpp src/maps.cpp src/props.cpp tests/fake_net.cpp tests/match_test.cpp \
    -o "$out"
"$out"
