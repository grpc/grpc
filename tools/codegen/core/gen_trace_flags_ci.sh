#!/bin/bash
# Copyright 2024 The gRPC authors.
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
# cd to repo root
dir=$(dirname "${0}")
cd "${dir}/../../.."

tools/bazel run --cxxopt='-std=c++17' tools/codegen/core:generate_trace_flags -- \
 --trace_flags_yaml=$(pwd)/src/core/lib/debug/trace_flags.yaml \
 --header_path=$(pwd)/src/core/lib/debug/trace_flags.h \
 --cpp_path=$(pwd)/src/core/lib/debug/trace_flags.cc \
 --markdown_path=$(pwd)/doc/trace_flags.md

TEST="" tools/distrib/clang_format_code.sh

output=$(git diff)
if [[ -n "$output" ]]; then
    echo "Trace flags need to be generated. Please run tools/codegen/core/generate_trace_flags_main.cc"
    echo $output
    exit 1
fi
