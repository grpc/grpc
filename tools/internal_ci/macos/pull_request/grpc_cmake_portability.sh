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

source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# configure ccache
source tools/internal_ci/helper_scripts/prepare_ccache_rc
source tools/internal_ci/helper_scripts/prepare_ccache_symlinks_rc

# Run CMake build with default compiler (linking grpc_cli)
python3 tools/run_tests/run_tests.py -l c++ -c dbg --compiler default --build_only --cmake_configure_extra_args="-DgRPC_BUILD_TESTS=OFF"
set RUNTESTS_EXITCODE=$?

# kill port_server.py to prevent the build from freezing
ps aux | grep port_server\\.py | awk '{print $2}' | xargs kill -9 || true

exit $RUNTESTS_EXITCODE