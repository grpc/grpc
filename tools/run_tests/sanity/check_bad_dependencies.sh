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

# Make sure that there is no path from known unsecure libraries and targets
# to an SSL library. Any failure among these will make the script fail.

test "$(bazel query 'somepath("//:grpc_unsecure", "//external:libssl")' 2>/dev/null | wc -l)" -eq 0 || exit 1
test "$(bazel query 'somepath("//:grpc++_unsecure", "//external:libssl")' 2>/dev/null | wc -l)" -eq 0 || exit 1
test "$(bazel query 'somepath("//:grpc++_codegen_proto", "//external:libssl")' 2>/dev/null | wc -l)" -eq 0 || exit 1
test "$(bazel query 'somepath("//test/cpp/microbenchmarks:helpers", "//external:libssl")' 2>/dev/null | wc -l)" -eq 0 || exit 1

# Make sure that core doesn't depend on anything in C++ library

test "$(bazel query 'deps("//:grpc")' 2>/dev/null | grep -Ec 'src/cpp|include/grpcpp')" -eq 0 || exit 1

exit 0

