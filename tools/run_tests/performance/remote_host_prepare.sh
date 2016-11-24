#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -ex

cd $(dirname $0)/../../..

# TODO(jtattermusch): To be sure there are no running processes that would
# mess with the results, be rough and reboot the slave here
# and wait for it to come back online.
ssh "${USER_AT_HOST}" "killall -9 qps_worker dotnet mono node ruby worker || true"

# On Windows, killall is not supported & we need to kill all pending workers
# before attempting to delete the workspace
ssh "${USER_AT_HOST}" "ps -e | egrep 'qps_worker|dotnet' | awk '{print $1}' | xargs kill -9 || true"

# cleanup after previous builds
ssh "${USER_AT_HOST}" "rm -rf ~/performance_workspace && mkdir -p ~/performance_workspace"

# push the current sources to the slave and unpack it.
scp ../grpc.tar "${USER_AT_HOST}:~/performance_workspace"
# Windows workaround: attempt to untar twice, first run is going to fail
# with symlink creation error(s).
ssh "${USER_AT_HOST}" "tar -xf ~/performance_workspace/grpc.tar -C ~/performance_workspace || tar -xf ~/performance_workspace/grpc.tar -C ~/performance_workspace"

# For consistency with local run, invoke the kill_workers script remotely.
ssh "${USER_AT_HOST}" "~/performance_workspace/grpc/tools/run_tests/performance/kill_workers.sh"
