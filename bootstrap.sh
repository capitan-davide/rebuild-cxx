#!/usr/bin/env bash
#
# Compiles the build program (build.cpp) once and runs it. From then on just
# run ./build: it rebuilds itself when build.cpp or rebuild_cxx.cpp change.
#
# Usage: ./bootstrap.sh [args for build...]
#
# The compiler must be clang++: ./build rebuilds itself from the command line
# recorded by -frecord-command-line.
#
set -euo pipefail

cd "$(dirname "$0")"

set -x
clang++ -std=c++23 -frecord-command-line build.cpp rebuild_cxx.cpp -o build
exec ./build "$@"
