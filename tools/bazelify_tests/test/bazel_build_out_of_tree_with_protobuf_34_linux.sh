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

cd test/bazel_build_out_of_tree/protobuf_34

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

# Protobuf 33/34 compatibility.
$GIT_ROOT/tools/bazel build "@grpc//test/cpp/util:grpc++_proto_reflection_desc_db"
