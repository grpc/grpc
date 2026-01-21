#!/bin/bash
# Copyright 2025 The gRPC Authors
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

cd test/bzlmod/

# Build the same targets as .bcr/presubmit.yml so we are more confident with
# releases.
$GIT_ROOT/tools/bazel \
    build \
    -- \
    "@grpc" \
    "@grpc//:grpc++" \
    "@grpc//:grpc++_reflection" \
    "@grpc//:grpc++_test" \
    "@grpc//:grpc++_unsecure" \
    "@grpc//:grpc_opencensus_plugin" \
    "@grpc//:grpc_security_base" \
    "@grpc//:grpc_unsecure" \
    "@grpc//:grpcpp_admin" \
    "@grpc//:grpcpp_channelz" \
    "@grpc//:grpcpp_csds" \
    "@grpc//:grpcpp_orca_service" \
    "@grpc//examples/protos/..."

# Adapted from tools/bazelify_tests/test/bazel_build_with_bzlmod_linux.sh
# Some nobuild tests for bzlmod dependency check.
$GIT_ROOT/tools/bazel \
    build \
    --nobuild \
    -- \
    "@grpc//:all" \
    "-@grpc//:grpcpp_csm_observability"

$GIT_ROOT/tools/bazel \
    build \
    --nobuild \
    -- \
    "@grpc//examples/cpp/..." \
    "-@grpc//examples/cpp/csm/..."
