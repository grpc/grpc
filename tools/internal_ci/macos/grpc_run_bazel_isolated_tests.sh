#!/usr/bin/env bash
# Copyright 2019 The gRPC Authors
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

# change to grpc repo root
cd $(dirname $0)/../../..

./tools/run_tests/start_port_server.py

# BUILD ONLY TEST!
# TODO(jtattermusch): the test has been disabled since it was highly flaky for a very long time (b/242971695).
# Re-enable the test (change "bazel build" to "bazel test") once it's passing reliably.
# run cfstream_test separately because it messes with the network
# The "local" execution strategy is required because the test runs sudo and that doesn't work in a sandboxed environment (the default on mac)
tools/bazel build $RUN_TESTS_FLAGS --genrule_strategy=local --test_output=all --copt="-DGRPC_CFSTREAM=1" //test/cpp/end2end:cfstream_test

# Missing the /var/db/ntp-kod file may breaks the ntp synchronization.
# Create the file and change the ownership to root before NTP sync.
# TODO(yulin-liang): investigate how to run time_jump_test without needing to mess with the system time directly.
# See b/166245303 
sudo touch /var/db/ntp-kod
sudo chown root:wheel /var/db/ntp-kod
# Make sure time is in sync before running time_jump_test because the test does
# NTP sync before exiting. Bazel gets confused if test end time < start time.
sudo sntp -sS pool.ntp.org

# BUILD ONLY TEST!
# TODO(jtattermusch): the test has been disabled since it was highly flaky for a very long time (b/242971695).
# Re-enable the test (change "bazel build" to "bazel test") once it's passing reliably.
# run time_jump_test separately because it changes system time
# The "local" execution strategy is required because the test runs sudo and that doesn't work in a sandboxed environment (the default on mac)
tools/bazel build $RUN_TESTS_FLAGS --genrule_strategy=local --test_output=all //test/cpp/common:time_jump_test

# kill port_server.py to prevent the build from freezing
ps aux | grep port_server\\.py | awk '{print $2}' | xargs kill -9

