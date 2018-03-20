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

[ -f $BINDIR/interop_server ] || {
    echo >&2 "Can't find the test server. Make sure run_tests.py is making" \
             "interop_server before calling this script."
    exit 1
}
$BINDIR/interop_server --port=5050 --max_send_message_size=8388608 &
$BINDIR/interop_server --port=5051 --max_send_message_size=8388608 --use_tls &
# Kill them when this script exits.
trap 'kill -9 `jobs -p` ; echo "EXIT TIME:  $(date)"' EXIT

set -o pipefail

# xcodebuild is very verbose. We filter its output and tell Bash to fail if any
# element of the pipe fails.
# TODO(jcanizales): Use xctool instead? Issue #2540.
XCODEBUILD_FILTER='(^CompileC |^Ld |^ *[^ ]*clang |^ *cd |^ *export |^Libtool |^ *[^ ]*libtool |^CpHeader |^ *builtin-copy )'

echo "TIME:  $(date)"

# Retry the test for up to 3 times when return code is 65, due to Xcode issue:
# http://www.openradar.me/29785686
# The issue seems to be a connectivity issue to Xcode simulator so only retry
# the first xcodebuild command
retries=0
while [ $retries -lt 3 ]; do
  return_code=0
  out=$(xcodebuild \
        -workspace Tests.xcworkspace \
        -scheme AllTests \
        -destination name="iPhone 6" \
        HOST_PORT_LOCALSSL=localhost:5051 \
        HOST_PORT_LOCAL=localhost:5050 \
        HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
        test 2>&1 \
        | egrep -v "$XCODEBUILD_FILTER" \
        | egrep -v '^$' \
        | egrep -v "(GPBDictionary|GPBArray)" - ) || return_code=$?
  if [ $return_code == 65 ] && [[ $out == *"DTXProxyChannel error 1"* ]]; then
    echo "$out"
    echo "Failed with code 65 (DTXProxyChannel error 1); retry."
    retries=$(($retries+1))
  elif [ $return_code == 0 ]; then
    echo "$out"
    break
  else
    echo "$out"
    echo "Failed with code $return_code."
    exit 1
  fi
done
if [ $retries == 3 ]; then
  echo "Failed with code 65 for 3 times; abort."
  exit 1
fi

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CoreCronetEnd2EndTests \
    -destination name="iPhone 6" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CoreCronetEnd2EndTests_Asan \
    -destination name="iPhone 6" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CoreCronetEnd2EndTests_Tsan \
    -destination name="iPhone 6" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CronetUnitTests \
    -destination name="iPhone 6" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme InteropTestsRemoteWithCronet \
    -destination name="iPhone 6" \
    HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

exit 0
