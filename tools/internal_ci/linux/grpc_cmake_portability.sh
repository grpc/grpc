#!/bin/bash
# Copyright 2026 gRPC authors.
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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Run CMake builds with old compilers and default compiler
# We use --build_only and -DgRPC_BUILD_TESTS=OFF to only build library and grpc_cli.

# GCC 8
python3 tools/run_tests/run_tests.py -l c++ -c dbg --compiler gcc8 --build_only --cmake_configure_extra_args="-DgRPC_BUILD_TESTS=OFF"

# GCC 10.2
python3 tools/run_tests/run_tests.py -l c++ -c dbg --compiler gcc10.2 --build_only --cmake_configure_extra_args="-DgRPC_BUILD_TESTS=OFF"

# Default
python3 tools/run_tests/run_tests.py -l c++ -c dbg --compiler default --build_only --cmake_configure_extra_args="-DgRPC_BUILD_TESTS=OFF"