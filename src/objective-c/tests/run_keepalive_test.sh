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

set -ex

cd $(dirname $0)

BAZEL=../../../tools/bazel

INTEROP=../../../bazel-bin/test/cpp/interop/interop_server

[ -d Tests.xcworkspace ] || {
    ./build_tests.sh
}

[ -f $INTEROP ] || {
    $BAZEL build //test/cpp/interop:interop_server
}

[ -z "$(ps aux |egrep 'port_server\.py.*-p\s32766')" ] && {
    echo >&2 "Can't find the port server. Start port server with tools/run_tests/start_port_server.py."
    exit 1
}

PLAIN_PORT=$(curl localhost:32766/get)
TLS_PORT=$(curl localhost:32766/get)

# start interop_server for plaintext and interop_server for TLS on random ports obtained
# from the port server with --ack_pings=false for keepalive testing.
$INTEROP --port=$PLAIN_PORT --max_send_message_size=8388608 --ack_pings=false &
$INTEROP --port=$TLS_PORT --max_send_message_size=8388608 --use_tls --ack_pings=false &

function finish {
    kill -9 `jobs -p`
    echo "EXIT TIME:  $(date)"
}
trap finish EXIT

set -o pipefail  # preserve xcodebuild exit code when piping output

if [ -z $PLATFORM ]; then
DESTINATION='platform=iOS Simulator,name=iPhone 11'
elif [ $PLATFORM == ios ]; then
DESTINATION='platform=iOS Simulator,name=iPhone 11'
elif [ $PLATFORM == macos ]; then
DESTINATION='platform=macOS'
elif [ $PLATFORM == tvos ]; then
DESTINATION='platform=tvOS Simulator,name=Apple TV'
fi

if [ -z $SCHEME ]; then
SCHEME='InteropTests'
fi

XCODEBUILD_FLAGS="
  -only-testing:${SCHEME}/InteropTestsLocalCleartext/testKeepaliveWithV2API
  IPHONEOS_DEPLOYMENT_TARGET=15.0
  ONLY_ACTIVE_ARCH=YES
"

XCODEBUILD_FILTER_OUTPUT_SCRIPT="./xcodebuild_filter_output.sh"

TEST_DEFS="HOST_PORT_LOCAL=localhost:$PLAIN_PORT \
HOST_PORT_LOCALSSL=localhost:$TLS_PORT \
HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
GRPC_RUN_KEEPALIVE_TEST=1 \
GCC_OPTIMIZATION_LEVEL=s"

export GRPC_RUN_KEEPALIVE_TEST="true"

time xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme $SCHEME \
    -destination "${DESTINATION}" \
    GCC_PREPROCESSOR_DEFINITIONS='$GCC_PREPROCESSOR_DEFINITIONS'" $TEST_DEFS" \
    build-for-testing \
    ${XCODEBUILD_FLAGS} \
    | "${XCODEBUILD_FILTER_OUTPUT_SCRIPT}"

XCTEST=$(xcrun --find xctest || echo "/Applications/Xcode.app/Contents/Developer/usr/bin/xctest")
BUNDLE_PATH=$(find ~/Library/Developer/Xcode/DerivedData/Tests-* -name "MacTests.xctest" 2>/dev/null | head -n 1)
export HOST_PORT_LOCAL="localhost:$PLAIN_PORT"
export HOST_PORT_LOCALSSL="localhost:$TLS_PORT"
"$XCTEST" -XCTest "InteropTestsLocalCleartext/testKeepaliveWithV2API" "$BUNDLE_PATH"
