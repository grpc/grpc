#!/bin/bash
# Copyright 2026 The gRPC Authors
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

GIT_ROOT="$(pwd)"

cd test/bazel_build_out_of_tree/proto_toolchain_resolution

# Build minimal downstream cc / python / objc gRPC codegen targets with
# --incompatible_enable_proto_toolchain_resolution enabled (set in this module's
# .bazelrc). This guards the protoc toolchain migration in
# bazel/private/proto_toolchain_helpers.bzl.
$GIT_ROOT/tools/bazel \
    test \
    -- \
    "//:proto_toolchain_resolution_build_test"
