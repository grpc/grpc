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

# env variable values extracted from PythonArtifact in tools/run_tests/artifacts/artifact_targets.py
# TODO(jtattermusch): find a better way of configuring the python artifact build (the current approach mostly serves as a demonstration)
export PYTHON=/opt/python/cp313-cp313/bin/python
export PIP=/opt/python/cp313-cp313/bin/pip
export GRPC_SKIP_PIP_CYTHON_UPGRADE=TRUE
export GRPC_RUN_AUDITWHEEL_REPAIR=TRUE
export GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS=TRUE

# Without this python cannot find the c++ compiler
# TODO(jtattermusch): find better solution to prevent bazel from
# restricting path contents
export PATH="/opt/rh/devtoolset-10/root/usr/bin:$PATH"

# set build parallelism to fit the machine configuration of bazelified tests RBE pool.
export GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS=8

mkdir -p artifacts

ARTIFACTS_OUT=artifacts tools/run_tests/artifacts/build_artifact_python.sh
