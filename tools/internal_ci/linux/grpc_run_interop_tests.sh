#!/bin/bash
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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

export LANG=en_US.UTF-8

# Enter the gRPC repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc
source tools/internal_ci/helper_scripts/prepare_build_interop_rc

# configure ccache
source tools/internal_ci/helper_scripts/prepare_ccache_rc

in_array() {
    local target="$1"
    shift
    for element; do
        if [[ "$element" == "$target" ]]; then
            return 0 # True (found)
        fi
    done
    return 1 # False (not found)
}

if in_array "--mcs_cs" $RUN_TESTS_FLAGS; then
  export GRPC_EXPERIMENTAL_MAX_CONCURRENT_STREAMS_CONNECTION_SCALING=true
  export GRPC_EXPERIMENTS=subchannel_connection_scaling
fi
tools/run_tests/run_interop_tests.py $RUN_TESTS_FLAGS
