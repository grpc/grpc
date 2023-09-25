#!/usr/bin/env bash
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

# Build all basic targets using the strict warning option which leverages the
# clang compiler to check if sources can pass a set of warning options.
# For now //examples/android/binder/ are excluded because it needs Android
# SDK/NDK to be installed to build

set -ex

echo "The "bazel build C/C++" tests have been migrated to bazelified tests under tools/bazelify_tests."
echo "This job is now a no-op".
