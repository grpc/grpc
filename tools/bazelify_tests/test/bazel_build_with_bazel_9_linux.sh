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

GIT_ROOT="$(dirname $0)/../../.."

OVERRIDE_BAZEL_VERSION=9.1.0 \
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
