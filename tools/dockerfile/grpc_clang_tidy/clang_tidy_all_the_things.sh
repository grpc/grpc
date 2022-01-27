#!/bin/sh
# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

# clang format command
CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

cd ${CLANG_TIDY_ROOT}

# run clang tidy for all source files
cat compile_commands.json | jq -r '.[].file' \
  | grep -E "(^include/|^src/core/|^src/cpp/|^test/core/|^test/cpp/)" \
  | grep -v -E "/upb-generated/|/upbdefs-generated/" \
  | sort \
  | xargs tools/distrib/run_clang_tidy.py "$@"
