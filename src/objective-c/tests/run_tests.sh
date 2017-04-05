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

# xcodebuild is very verbose. We filter its output and tell Bash to fail if any
# element of the pipe fails.
# TODO(jcanizales): Use xctool instead? Issue #2540.
set -o pipefail
XCODEBUILD_FILTER='(^===|^\*\*|\bfatal\b|\berror\b|\bwarning\b|\bfail)'
echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme AllTests \
    -destination name="iPhone 6" \
    test | xcpretty

echo "TIME:  $(date)"
xcodebuild \
    -workspace Tests.xcworkspace \
    -scheme CoreCronetEnd2EndTests \
    -destination name="iPhone 6" \
    test | xcpretty

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
    test | xcpretty
