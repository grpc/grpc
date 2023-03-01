#!/bin/bash
# Copyright 2019 gRPC authors.
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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

# TODO(tonyzhehaolu):
# For future use when Xcode is upgraded and tvos_unit_test is fully functional

set -ev

cd $(dirname $0)

BAZEL=../../../tools/bazel

INTEROP=../../../bazel-bin/test/cpp/interop/interop_server

[ -f $INTEROP ] || {
    $BAZEL build //test/cpp/interop:interop_server
}
[ -f $INTEROP ] || exit 1

[ -z "$(ps aux |egrep 'port_server\.py.*-p\s32766')" ] && {
    echo >&2 "Can't find the port server. Start port server with tools/run_tests/start_port_server.py."
    exit 1
}

PLAIN_PORT=$(curl localhost:32766/get)
TLS_PORT=$(curl localhost:32766/get)

$INTEROP --port=$PLAIN_PORT --max_send_message_size=8388608 &
$INTEROP --port=$TLS_PORT --max_send_message_size=8388608 --use_tls &

trap 'kill -9 `jobs -p` ; echo "EXIT TIME:  $(date)"' EXIT

time $BAZEL test --ios_multi_cpus=x86_64,sim_arm64 \
    --test_env HOST_PORT_LOCALSSL=localhost:$TLS_PORT \
    --test_env HOST_PORT_LOCAL=localhost:$PLAIN_PORT \
    $SCHEME
