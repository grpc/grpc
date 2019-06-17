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

set -ex

# Use bazelisk to download the right bazel version
wget https://github.com/bazelbuild/bazelisk/releases/download/v0.0.7/bazelisk-linux-amd64
chmod u+x bazelisk-linux-amd64

# We want bazelisk to run the latest stable version
export USE_BAZEL_VERSION=latest
# Use bazelisk instead of our usual //tools/bazel wrapper
mv bazelisk-linux-amd64 github/grpc/tools/bazel

EXTRA_FLAGS="--config=opt --cache_test_results=no"
github/grpc/tools/internal_ci/linux/grpc_bazel_on_foundry_base.sh "${EXTRA_FLAGS}"
