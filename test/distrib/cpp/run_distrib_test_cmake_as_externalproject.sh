#!/bin/bash
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

cd "$(dirname "$0")/../../.."

# Install openssl (to use instead of boringssl)
apt-get update && apt-get install -y libssl-dev

# To increase the confidence that gRPC installation works without depending on
# too many submodules unnecessarily, just wipe out contents of most submodules
# before starting the test.
rm -r third_party/benchmark/* || true
rm -r third_party/bloaty/* || true
rm -r third_party/boringssl-with-bazel/* || true
rm -r third_party/googletest/* || true

# Build helloworld example using cmake superbuild
cd examples/cpp/helloworld/cmake_externalproject
mkdir -p cmake/build
cd cmake/build
cmake ../..
make
