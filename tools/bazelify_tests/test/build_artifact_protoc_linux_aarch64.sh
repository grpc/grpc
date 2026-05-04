#!/bin/bash
# Copyright 2024 The gRPC Authors
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

# compile/link options extracted from ProtocArtifact in tools/run_tests/artifacts/artifact_targets.py
# NOTE: -Wl,-z,max-page-size=65536 is required because the manylinux2014
# cross-compilation toolchain may produce binaries with 4KB page alignment,
# but ARM64 Linux kernels (e.g. RHEL) can use 64KB pages. Without this flag
# the protoc binary will segfault (exit code 139) on such systems.
# See https://github.com/grpc/grpc/issues/38538
# and https://github.com/pypa/manylinux/issues/735.
export LDFLAGS="${LDFLAGS} -static-libgcc -static-libstdc++ -Wl,-z,max-page-size=65536 -s"
# set build parallelism to fit the machine configuration of bazelified tests RBE pool.
export GRPC_PROTOC_BUILD_COMPILER_JOBS=8

# Without this cmake find the c++ compiler
# find better solution to prevent bazel from restricting path contents
export PATH="/opt/rh/devtoolset-10/root/usr/bin:/usr/xcc/aarch64-unknown-linux-gnu/bin:$PATH"

mkdir -p artifacts
env
ARTIFACTS_OUT=artifacts tools/run_tests/artifacts/build_artifact_protoc.sh
