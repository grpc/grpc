#!/bin/sh
#
# Copyright 2015 The Bazel Authors. All rights reserved.
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
#
# Ship the environment to the C++ action
#
set -eu

OUTPUT=

parse_option() {
    opt=$1
    if [ "$OUTPUT" = "1" ]; then
        OUTPUT=$opt
    elif [ "$opt" = "-o" ]; then
        # output is coming
        OUTPUT=1
    fi
}

# parse the option list
for i in "$@"; do
    case $i in
        @*)
            file=${i#@}
            if [ -r "$file" ]; then
                while IFS= read -r opt; do
                    parse_option "$opt"
                done < "$file" || exit 1
            fi
            ;;
        *)
            parse_option "$i"
            ;;
    esac
done

# Set-up the environment


# Call the C++ compiler
/usr/lib/llvm-19/bin/clang "$@"

# Generate an empty file if header processing succeeded.
case $OUTPUT in
    *.h.processed)
        : > "$OUTPUT"
        ;;
esac
