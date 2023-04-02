#!/usr/bin/env bash
# Copyright 2022 The gRPC authors.
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

set -exo pipefail

SERVER_PID=""
SERVER_TIMEOUT=10

SERVER_OUTPUT=$(mktemp)

function cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID"
  fi
}

function fail() {
  echo "$1" >/dev/stderr
  echo "Failed." >/dev/stderr
  exit 1
}

function await_server() {
  TIME=0
  while [ ! -s "$SERVER_OUTPUT" ]; do
    if [ "$TIME" == "$SERVER_TIMEOUT" ] ; then
      fail "Server not listening after $SERVER_TIMEOUT seconds."
    fi
    sleep 1
    TIME=$((TIME+1))
  done
  cat "$SERVER_OUTPUT"
}

trap cleanup SIGINT SIGTERM EXIT

./greeter_server >"$SERVER_OUTPUT" &
SERVER_PID=$!

SERVER_ADDRESS=$(await_server)

RESPONSE=$(./greeter_client --target="$SERVER_ADDRESS")
EXPECTED_RESPONSE="Greeter received: Hello world"

if [ "$RESPONSE" != "$EXPECTED_RESPONSE" ]; then
  fail "Received response \"$RESPONSE\" but expected \"$EXPECTED_RESPONSE\""
fi

echo "Success."
