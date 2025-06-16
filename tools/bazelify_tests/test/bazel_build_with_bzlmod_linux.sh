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

# Test if public targets are buildable without dev dependencies.
tools/bazel \
    build \
    --enable_bzlmod=true \
    --enable_workspace=false \
    --ignore_dev_dependency \
    -- \
    :all \
    -:grpcpp_csm_observability  # Needs google_cloud_cpp to be added to BCR

# Test if examples are buildable without dev dependencies.
tools/bazel \
    build \
    --enable_bzlmod=true \
    --enable_workspace=false \
    --ignore_dev_dependency \
    -- \
    //examples/cpp/... \
    -//examples/cpp/csm/...  # Needs grpcpp_csm_observability

# Test if a few basic tests can pass.
# This is a temporary sanity check covering essential features,
# to be replaced by a comprehensive test suite once the bzlmod migration is finished.
tools/bazel \
    test \
    --enable_bzlmod=true \
    --enable_workspace=false \
    -- \
    //test/core/config:all \
    //test/cpp/common:all

# Test if public targets are buildable with openssl and without dev
# dependencies.
tools/bazel \
    build \
    --enable_bzlmod=true \
    --enable_workspace=false \
    --ignore_dev_dependency \
    --define=//third_party:grpc_use_openssl=true \
    -- \
    :all \
    -:grpcpp_csm_observability  # Needs google_cloud_cpp to be added to BCR
