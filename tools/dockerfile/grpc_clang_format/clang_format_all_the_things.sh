#!/bin/bash
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

# change to root directory
cd $(dirname $0)/../../..
REPO_ROOT=$(pwd)

# Get number of CPU cores
if [[ "$OSTYPE" == "darwin"* ]]; then
  NPROC=$(sysctl -n hw.ncpu)
else
  NPROC=$(nproc)
fi

# Get the list of files to format
if [ "$CHANGED_FILES" != "" ]; then
  FILES_TO_FORMAT="$CHANGED_FILES"
else
  FILES_TO_FORMAT=$(find . -name "*.h" -o -name "*.cc" | grep -v "third_party" | grep -v "bazel-" | grep -v "build" | grep -v "cmake" | grep -v "protobuf" | grep -v "upb" | grep -v "utf8_range" | grep -v "xxhash" | grep -v "zlib" | grep -v "cares" | grep -v "re2" | grep -v "abseil-cpp" | grep -v "boringssl" | grep -v "benchmark" | grep -v "gtest" | grep -v "gmock" | grep -v "protoc" | grep -v "protobuf" | grep -v "upb" | grep -v "utf8_range" | grep -v "xxhash" | grep -v "zlib" | grep -v "cares" | grep -v "re2" | grep -v "abseil-cpp" | grep -v "boringssl" | grep -v "benchmark" | grep -v "gtest" | grep -v "gmock")
fi

# Format the files
clang-format -i -style=file $FILES_TO_FORMAT
