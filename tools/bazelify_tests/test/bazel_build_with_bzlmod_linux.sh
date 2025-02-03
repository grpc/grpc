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

tools/bazel \
    build \
    --enable_bzlmod=true \
    --enable_workspace=false \
    :grpc \
    :grpc_unsecure \
    :grpc_opencensus_plugin \
    :grpc_security_base \
    :grpc++ \
    :grpc++_unsecure \
    :grpc++_reflection \
    :grpc++_test \
    :grpcpp_admin \
    :grpcpp_channelz \
    :grpcpp_csds \
    :grpcpp_orca_service \
    :grpcpp_gcp_observability
    :grpcpp_csm_observability  # Needed google_cloud_cpp to be added to BCR
