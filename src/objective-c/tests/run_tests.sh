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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -ev

cd $(dirname $0)

# Run the tests server.

BINDIR=../../../bins/$CONFIG
PROTOC=$BINDIR/protobuf/protoc
PLUGIN=$BINDIR/grpc_objective_c_plugin

rm -rf PluginTest/*pb*

# Verify the output proto filename
eval $PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --objc_out=PluginTest \
    --grpc_out=PluginTest \
    -I PluginTest \
    -I ../../../third_party/protobuf/src \
    PluginTest/*.proto

[ -e ./PluginTest/TestDashFilename.pbrpc.h ] || {
    echo >&2 "protoc outputs wrong filename."
    exit 1
}

# Verify names of the imported protos in generated code
[ "`cat PluginTest/TestDashFilename.pbrpc.h |
    egrep '#import ".*\.pb(objc|rpc)\.h"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}
[ "`cat PluginTest/TestDashFilename.pbrpc.m |
    egrep '#import ".*\.pb(objc|rpc)\.m"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

[ -f $BINDIR/interop_server ] || {
    echo >&2 "Can't find the test server. Make sure run_tests.py is making" \
             "interop_server before calling this script."
    exit 1
}
$BINDIR/interop_server --port=5050 --max_send_message_size=8388608 &
$BINDIR/interop_server --port=5051 --max_send_message_size=8388608 --use_tls &
# Kill them when this script exits.
trap 'kill -9 `jobs -p` ; echo "EXIT TIME:  $(date)"' EXIT

# xcodebuild is very verbose. We filter its output and tell Bash to fail if any
# element of the pipe fails.
# TODO(jcanizales): Use xctool instead? Issue #2540.
set -o pipefail
XCODEBUILD_FILTER='(^===|^\*\*|\bfatal\b|\berror\b|\bwarning\b|\bfail|\bpassed\b)'
echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme AllTests \
    -destination name="iPhone 6" \
    HOST_PORT_LOCALSSL=localhost:5051 \
    HOST_PORT_LOCAL=localhost:5050 \
    HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
    test \
    | egrep "$XCODEBUILD_FILTER" \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CoreCronetEnd2EndTests \
    -destination name="iPhone 6" \
    test \
    | egrep "$XCODEBUILD_FILTER" \
    | egrep -v "(GPBDictionary|GPBArray)" -

# Temporarily disabled for (possible) flakiness on Jenkins.
# Fix or reenable after confirmation/disconfirmation that it is the source of
# Jenkins problem.

# echo "TIME:  $(date)"
# xcodebuild \
#     -workspace Tests.xcworkspace \
#     -scheme CronetUnitTests \
#     -destination name="iPhone 6" \
#     test | xcpretty

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme InteropTestsRemoteWithCronet \
    -destination name="iPhone 6" \
    HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
    test \
    | egrep "$XCODEBUILD_FILTER" \
    | egrep -v "(GPBDictionary|GPBArray)" -
