#!/bin/bash
# Copyright 2015 gRPC authors.
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

cd "$(dirname "$0")/../../.."

# TODO(jtattermusch): To be sure there are no running processes that would
# mess with the results, be rough and reboot the slave here
# and wait for it to come back online.
ssh "${USER_AT_HOST}" "killall -9 qps_worker dotnet mono node ruby worker || true"

# On Windows, killall is not supported & we need to kill all pending workers
# before attempting to delete the workspace
ssh "${USER_AT_HOST}" "ps -e | egrep 'qps_worker|dotnet' | awk '{print \$1}' | xargs kill -9 || true"

# cleanup after previous builds
ssh "${USER_AT_HOST}" "rm -rf ~/performance_workspace && mkdir -p ~/performance_workspace"

# push the current sources to the slave and unpack it.
scp ../grpc.tar "${USER_AT_HOST}:~/performance_workspace"
# Windows workaround: attempt to untar twice, first run is going to fail
# with symlink creation error(s).
ssh "${USER_AT_HOST}" "tar -xf ~/performance_workspace/grpc.tar -C ~/performance_workspace || tar -xf ~/performance_workspace/grpc.tar -C ~/performance_workspace"

# For consistency with local run, invoke the kill_workers script remotely.
# shellcheck disable=SC2088
ssh "${USER_AT_HOST}" "~/performance_workspace/grpc/tools/run_tests/performance/kill_workers.sh"
