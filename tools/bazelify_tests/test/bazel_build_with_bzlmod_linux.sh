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
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    build \
    -- \
    :all \
    -:grpcpp_csm_observability  # Needs google_cloud_cpp to be added to BCR

# Test if examples are buildable without dev dependencies.
tools/bazel \
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    build \
    -- \
    //examples/cpp/... \
    -//examples/cpp/csm/...  # Needs grpcpp_csm_observability

# Test if a few basic tests can pass.
# This is a temporary sanity check covering essential features,
# to be replaced by a comprehensive test suite once the bzlmod migration is finished.
tools/bazel \
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    test \
    --ignore_dev_dependency=false \
    -- \
    //test/core/config:all \
    //test/core/util:all \
    //test/cpp/common:all

# Use --nobuild flag to trigger bazel dependency analysis but skip C++
# compilation.
# TODO(weizheyuan): Re-enable the full build (by removing --nobuild)
# once it no longer causes CI timeouts.
tools/bazel \
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    build \
    --nobuild \
    --ignore_dev_dependency=false \
    -- \
    //test/... \
    -//test/cpp/ext/... \
    -//test/cpp/interop/...

# Test if public targets are buildable with openssl and without dev
# dependencies.
tools/bazel \
    --bazelrc=tools/remote_build/linux_bzlmod.bazelrc \
    build \
    --define=//third_party:grpc_use_openssl=true \
    -- \
    :all \
    -:grpcpp_csm_observability  # Needs google_cloud_cpp to be added to BCR
