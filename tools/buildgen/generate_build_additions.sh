#!/bin/sh
# Copyright 2015 gRPC authors.
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

set -e

gen_build_yaml_dirs="  \
  src/abseil-cpp       \
  src/boringssl        \
  src/benchmark        \
  src/proto            \
  src/re2              \
  src/upb              \
  src/zlib             \
  src/c-ares           \
  test/cpp/naming"


gen_build_files=""
for gen_build_yaml in $gen_build_yaml_dirs
do
  output_file=$(mktemp /tmp/gen_$(echo $gen_build_yaml | tr '/' '_').yaml.XXXXX)
  python3 $gen_build_yaml/gen_build_yaml.py > $output_file
  gen_build_files="$gen_build_files $output_file"
done
