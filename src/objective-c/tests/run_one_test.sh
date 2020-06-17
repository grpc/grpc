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

BAZEL=../../../tools/bazel

INTEROP=../../../bazel-out/darwin-fastbuild/bin/test/cpp/interop/interop_server

[ -d Tests.xcworkspace ] || {
    ./build_tests.sh
}

[ -f $INTEROP ] || {
    BAZEL build //test/cpp/interop:interop_server
}

[ -z "$(ps aux |egrep 'port_server\.py.*-p\s32766')" ] && {
    echo >&2 "Can't find the port server. Start port server with tools/run_tests/start_port_server.py."
    exit 1
}

PLAIN_PORT=$(curl localhost:32766/get)
TLS_PORT=$(curl localhost:32766/get)

$INTEROP --port=$PLAIN_PORT --max_send_message_size=8388608 &
$INTEROP --port=$TLS_PORT --max_send_message_size=8388608 --use_tls &

# Create loopback aliases for iOS performance tests
if [ $SCHEME == PerfTests ] || [ $SCHEME == PerfTestsPosix ]; then
for ((i=2;i<11;i++))
do
    sudo ifconfig lo0 alias 127.0.0.$i up
done
fi

function finish {
    if [ $SCHEME == PerfTests ] || [ $SCHEME == PerfTestsPosix ]; then
    for ((i=2;i<11;i++))
    do
        sudo ifconfig lo0 -alias 127.0.0.$i
    done
    fi
    kill -9 `jobs -p`
    echo "EXIT TIME:  $(date)"
}
trap finish EXIT

set -o pipefail

XCODEBUILD_FILTER='(^CompileC |^Ld |^ *[^ ]*clang |^ *cd |^ *export |^Libtool |^ *[^ ]*libtool |^CpHeader |^ *builtin-copy )'

if [ -z $PLATFORM ]; then
DESTINATION='name=iPhone 8'
elif [ $PLATFORM == ios ]; then
DESTINATION='name=iPhone 8'
elif [ $PLATFORM == macos ]; then
DESTINATION='platform=macOS'
elif [ $PLATFORM == tvos ]; then
DESTINATION='platform=tvOS Simulator,name=Apple TV'
fi


xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme $SCHEME \
    -destination "$DESTINATION" \
    HOST_PORT_LOCALSSL=localhost:$TLS_PORT \
    HOST_PORT_LOCAL=localhost:$PLAIN_PORT \
    HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
    test \
    | ./verbose_time.sh \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

