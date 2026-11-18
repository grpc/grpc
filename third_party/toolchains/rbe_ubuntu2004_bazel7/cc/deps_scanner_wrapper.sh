#!/usr/bin/env bash
#
# Ship the environment to the C++ action
#
set -eu

# Set-up the environment


# Call the C++ compiler
/usr/local/bin/clang-scan-deps -format=p1689 -- /usr/local/bin/clang-19 "$@" >"$DEPS_SCANNER_OUTPUT_FILE"
