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

mkdir -p artifacts

# List all input artifacts we obtained for easier troubleshooting.
ls -lR input_artifacts

# Put the input artifacts where the legacy logic for building
# C# package expects to find them.
# See artifact_targets.py and package_targets.py for details.
# TODO(jtattermusch): get rid of the manual renames of artifact directories.
export EXTERNAL_GIT_ROOT="$(pwd)"
mv input_artifacts/artifact_protoc_linux_aarch64 input_artifacts/protoc_linux_aarch64 || true
mv input_artifacts/artifact_protoc_linux_x64 input_artifacts/protoc_linux_x64 || true
mv input_artifacts/artifact_protoc_linux_x86 input_artifacts/protoc_linux_x86 || true

# In the bazel workflow, we only have linux protoc artifact at hand,
# so we can only build a "singleplatform" version of the C# package.
export GRPC_CSHARP_BUILD_SINGLE_PLATFORM_NUGET=1

# TODO(jtattermusch): when building the C# nugets, the current git commit SHA
# is retrieved and stored as package metadata. But when running
# as bazelified test, this is not possible since we're not in a git
# workspace when running the build. This is ok for testing purposes
# but would be a problem if building a production package
# for the end users.

src/csharp/build_nuget.sh
