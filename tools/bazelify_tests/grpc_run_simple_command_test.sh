#!/bin/bash
# Copyright 2023 The gRPC Authors
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

ARCHIVE_WITH_SUBMODULES="$1"
shift

# Extract grpc repo archive
tar -xopf ${ARCHIVE_WITH_SUBMODULES}
cd grpc

# Override the "do not detect toolchain" setting that was set
# by the .bazelrc configuration for the remote build.
# TODO(jtattermusch): find a better solution to avoid breaking toolchain detection.
export BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=0

# Run command passed as args
"$@"
