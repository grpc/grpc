#!/usr/bin/env bash
# Copyright 2021 gRPC authors.
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

# Test structure borrowed with gratitude from
# https://github.com/grpc/grpc-go/tree/master/examples/features

set -eu

ROOT_DIR=../../..
SERVER_PORT=50051

export TMPDIR=$(mktemp -d)
trap "rm -rf ${TMPDIR}" EXIT

clean() {
  # loop a handful of times in case job shutdown is not immediate
  for i in {1..5}; do
    # kill all child jobs, even if some have ended themselves.
    jobs -p | xargs kill &>/dev/null || true
    # wait for jobs
    sleep 0.3
    if ! jobs | read; then
      return
    fi
  done
  echo "$(tput setaf 1) clean failed to kill tests $(tput sgr 0)"
  jobs
  exit 1
}

fail() {
  echo "$@" >&2
  clean
  exit 1
}

pass() {
  echo "$(tput setaf 2) $1 $(tput sgr 0)"
}

wait_for_server() {
  feature=$1
  wait_command=${SERVER_WAIT_COMMAND[$feature]:-${SERVER_WAIT_COMMAND["default"]}}
  echo "$(tput setaf 4) waiting for server to start $(tput sgr 0)"
  for i in {1..10}; do
    eval "$wait_command" 2>&1 &>/dev/null
    if [ $? -eq 0 ]; then
      pass "server started"
      return
    fi
    sleep 1
  done
  fail "cannot determine if server started"
}

FEATURES=(
  "unix_abstract"
)

declare -A SERVER_WAIT_COMMAND=(
  ["unix_abstract"]="lsof -U | grep '@grpc@abstract'"
  ["default"]="lsof -i :$SERVER_PORT | grep $SERVER_PORT"
)

declare -A EXPECTED_SERVER_OUTPUT=(
  ["unix_abstract"]="Server listening on unix-abstract:grpc%00abstract ... Echoing: arst"
)

declare -A EXPECTED_CLIENT_OUTPUT=(
  ["unix_abstract"]="Sending 'arst' to unix-abstract:grpc%00abstract ... Received: arst"
)

for feature in ${FEATURES[@]}; do
  echo "$(tput setaf 4) testing: $feature $(tput sgr 0)"
  bazel build //examples/cpp/features/${feature}:all || fail "failed to build $feature"

  SERVER_LOG="$(mktemp)"
  $ROOT_DIR/bazel-bin/examples/cpp/features/$feature/server &>$SERVER_LOG &

  wait_for_server $feature

  # TODO(hork): add a timeout to abort client?
  CLIENT_LOG="$(mktemp)"
  $ROOT_DIR/bazel-bin/examples/cpp/features/$feature/client &>$CLIENT_LOG

  if [ -n "${EXPECTED_SERVER_OUTPUT[$feature]}" ]; then
    if ! grep -q "${EXPECTED_SERVER_OUTPUT[$feature]}" $SERVER_LOG; then
      fail "server log missing output: ${EXPECTED_SERVER_OUTPUT[$feature]}
      got server log:
      $(cat $SERVER_LOG)
      got client log:
      $(cat $CLIENT_LOG)
      "
    else
      pass "server log contains expected output: ${EXPECTED_SERVER_OUTPUT[$feature]}"
    fi
  fi

  if [ -n "${EXPECTED_CLIENT_OUTPUT[$feature]}" ]; then
    if ! grep -q "${EXPECTED_CLIENT_OUTPUT[$feature]}" $CLIENT_LOG; then
      fail "client log missing output: ${EXPECTED_CLIENT_OUTPUT[$feature]}
      got server log:
      $(cat $SERVER_LOG)
      got client log:
      $(cat $CLIENT_LOG)
      "
    else
      pass "client log contains expected output: ${EXPECTED_CLIENT_OUTPUT[$feature]}"
    fi
  fi

  clean
done
