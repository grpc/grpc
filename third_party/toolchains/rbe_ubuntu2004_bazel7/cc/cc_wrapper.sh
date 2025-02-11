#!/bin/bash
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

function parse_option() {
    local -r opt="$1"
    if [[ "${OUTPUT}" = "1" ]]; then
        OUTPUT=$opt
    elif [[ "$opt" = "-o" ]]; then
        # output is coming
        OUTPUT=1
    fi
}

# let parse the option list
for i in "$@"; do
    if [[ "$i" = @* && -r "${i:1}" ]]; then
        while IFS= read -r opt
        do
            parse_option "$opt"
        done < "${i:1}" || exit 1
    else
        parse_option "$i"
    fi
done

# Set-up the environment


# Call the C++ compiler
/usr/local/bin/clang-19 "$@"

# Generate an empty file if header processing succeeded.
if [[ "${OUTPUT}" == *.h.processed ]]; then
  echo -n > "${OUTPUT}"
fi
