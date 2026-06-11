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

export OVERRIDE_BAZEL_VERSION=9.1.0
GIT_ROOT=$(realpath "$(dirname "$0")/../../..")

${GIT_ROOT}/tools/bazel \
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    build \
    --nobuild \
    -- \
    "//:grpc" \
    "//:grpc++_test" \
    "//:grpc++" \
    "//:grpc++_reflection" \
    "//:grpc++_unsecure" \
    "//:grpc_opencensus_plugin" \
    "//:grpc_security_base" \
    "//:grpc_unsecure" \
    "//:grpcpp_admin" \
    "//:grpcpp_channelz" \
    "//:grpcpp_csds" \
    "//:grpcpp_orca_service" \
    "//examples/protos/..."


# The out-of-tree test
cd ${GIT_ROOT}/test/bazel_build_out_of_tree/bazel_9

${GIT_ROOT}/tools/bazel \
    build \
    --nobuild \
    -- \
    "@grpc//:grpc" \
    "@grpc//:grpc++" \
    "@grpc//:grpc++_reflection" \
    "@grpc//:grpc++_unsecure" \
    "@grpc//:grpc_opencensus_plugin" \
    "@grpc//:grpc_security_base" \
    "@grpc//:grpc_unsecure" \
    "@grpc//:grpcpp_admin" \
    "@grpc//:grpcpp_channelz" \
    "@grpc//:grpcpp_csds" \
    "@grpc//:grpcpp_orca_service" \
    "@grpc//examples/protos/..."
