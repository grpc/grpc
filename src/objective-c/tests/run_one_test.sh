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

set -ev

cd $(dirname $0)

BINDIR=../../../bins/$CONFIG

[ -f $BINDIR/interop_server ] || {
    echo >&2 "Can't find the test server. Make sure run_tests.py is making" \
             "interop_server before calling this script."
    exit 1
}

[ -z "$(ps aux |egrep 'port_server\.py.*-p\s32766')" ] && {
    echo >&2 "Can't find the port server. Start port server with tools/run_tests/start_port_server.py."
    exit 1
}

PLAIN_PORT=$(curl localhost:32766/get)
TLS_PORT=$(curl localhost:32766/get)

$BINDIR/interop_server --port=$PLAIN_PORT --max_send_message_size=8388608 &
$BINDIR/interop_server --port=$TLS_PORT --max_send_message_size=8388608 --use_tls &

trap 'kill -9 `jobs -p` ; echo "EXIT TIME:  $(date)"' EXIT

set -o pipefail

XCODEBUILD_FILTER='(^CompileC |^Ld |^ *[^ ]*clang |^ *cd |^ *export |^Libtool |^ *[^ ]*libtool |^CpHeader |^ *builtin-copy )'

if [ -z $PLATFORM ]; then
DESTINATION='name=iPhone 8'
elif [ $PLATFORM == ios ]; then
DESTINATION='name=iPhone 8'
elif [ $PLATFORM == macos ]; then
DESTINATION='platform=macOS'
fi

xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme $SCHEME \
    -destination "$DESTINATION" \
    HOST_PORT_LOCALSSL=localhost:$TLS_PORT \
    HOST_PORT_LOCAL=localhost:$PLAIN_PORT \
    HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -
