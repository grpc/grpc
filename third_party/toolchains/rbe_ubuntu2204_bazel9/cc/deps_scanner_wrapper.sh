#!/usr/bin/env bash
#
# Ship the environment to the C++ action
#
set -eu

# Set-up the environment


# Call the C++ compiler
/usr/lib/llvm-19/bin/clang-scan-deps -format=p1689 -- /usr/lib/llvm-19/bin/clang "$@" >"$DEPS_SCANNER_OUTPUT_FILE"
