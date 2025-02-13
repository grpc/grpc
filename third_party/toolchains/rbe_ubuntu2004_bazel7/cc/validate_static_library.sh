#!/usr/bin/env bash
#
# Copyright 2023 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
set -euo pipefail

# Find all duplicate symbols in the given static library:
# 1. Use nm to list all global symbols in the library in POSIX format:
#    libstatic.a[my_object.o]: my_function T 1234 abcd
# 2. Use sed to transform the output to a format that can be sorted by symbol
#    name and is readable by humans:
#    my_object.o: T my_function
#    By using the `t` and `d` commands, lines for symbols of type U (undefined)
#    as well as V and W (weak) and their local lowercase variants are removed.
# 3. Use sort to sort the lines by symbol name.
# 4. Use uniq to only keep the lines corresponding to duplicate symbols.
# 5. Use c++filt to demangle the symbol names.
#    c++filt is applied to the duplicated symbols instead of using the -C flag
#    of nm because it is not in POSIX and demangled names may not be unique
#    (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=35201).
DUPLICATE_SYMBOLS=$(
  "/usr/bin/nm" -A -g -P  "$1" |
  sed -E -e 's/.*\[([^][]+)\]: (.+) ([A-TX-Z]) [a-f0-9]+ [a-f0-9]+/\1: \3 \2/g' -e t -e d |
  LC_ALL=C sort -k 3 |
  LC_ALL=C uniq -D -f 2 |
  "/usr/bin/c++filt")
if [[ -n "$DUPLICATE_SYMBOLS" ]]; then
  >&2 echo "Duplicate symbols found in $1:"
  >&2 echo "$DUPLICATE_SYMBOLS"
  exit 1
else
  touch "$2"
fi
