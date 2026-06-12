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

# env variable values extracted from PythonArtifact in tools/run_tests/artifacts/artifact_targets.py
# TODO(asheshvidyut): find a better way of configuring the python artifact build (the current approach mostly serves as a demonstration)
export PYTHON=/opt/python/cp310-cp310/bin/python
export PIP=/opt/python/cp310-cp310/bin/pip
export GRPC_SKIP_PIP_CYTHON_UPGRADE=TRUE
export GRPC_RUN_AUDITWHEEL_REPAIR=TRUE
export GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS=TRUE

# set build parallelism to fit the machine configuration of bazelified tests RBE pool.
export GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS=8

mkdir -p artifacts

export ARTIFACTS_OUT=artifacts
# gRPC requires a compiler that supports C++17. The default compiler in this
# build environment (likely an older manylinux image) is too old.
# We use 'scl enable devtoolset-10' to activate GCC 10 for the duration of
# the build. This differs from the main branch/legacy setup where artifact
# builds are usually run inside specific Docker images (managed by run_tests.py)
# that already have the correct environment enabled.
scl enable devtoolset-10 -- tools/run_tests/artifacts/build_artifact_python.sh
