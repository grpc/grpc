#!/bin/bash
#
#  Copyright 2025 gRPC authors.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

set +ex

export TMPDIR=$(mktemp -d)
trap "rm -rf ${TMPDIR}" EXIT

export SERVER_PORT=50051
export UNIX_ADDR=abstract-unix-socket

SERVER_PID=

clean () {
  echo "CLeaning server ${SERVER_PID}"
  if [[ -n $SERVER_PID ]] then
      kill -9 $SERVER_PID || true
  fi
  SERVER_PID=
  exit 0
}

fail () {
    echo "$(tput setaf 1) $1 $(tput sgr 0)"
    if [[ -n $SERVER_PID ]] then
      kill -9 $SERVER_PID || true
    fi
    SERVER_PID=
    exit 1
}

pass () {
    echo "$(tput setaf 2) $1 $(tput sgr 0)"
}

EXAMPLES=(
    "helloworld:greeter_callback"
)

declare -A SERVER_ARGS=(
    ["default"]="-port $SERVER_PORT"
)

declare -A CLIENT_ARGS=(
    ["default"]="-target localhost:$SERVER_PORT"
)

declare -A SERVER_WAIT_COMMAND=(
    ["default"]="lsof -i :$SERVER_PORT | grep $SERVER_PORT"
)

wait_for_server () {
    example=$1
    wait_command=${SERVER_WAIT_COMMAND[$example]:-${SERVER_WAIT_COMMAND["default"]}}
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

declare -A EXPECTED_SERVER_OUTPUT=(
    ["helloworld:greeter_callback"]=""
)

declare -A EXPECTED_CLIENT_OUTPUT=(
    ["helloworld:greeter_callback"]="Greeter received: Hello world"
)


for example in ${EXAMPLES[@]}; do
    echo "$(tput setaf 4) testing: ${example} $(tput sgr 0)"

    # Build server
    if ! bazel build  ${example}_server; then
        fail "failed to build server"
    else
        pass "successfully built server"
    fi

    # Start server
    SERVER_LOG="$(mktemp)"
    server_args=${SERVER_ARGS[$example]:-${SERVER_ARGS["default"]}}
    bazel run ${example}_server -- $server_args &> $SERVER_LOG  &
    SERVER_PID=$!
    echo "Server Pid : ${SERVER_PID}"

    wait_for_server $example

    # Build client
    if ! bazel build  ${example}_client; then
        fail "failed to build client"
    else
        pass "successfully built client"
    fi

    # Start client
    CLIENT_LOG="$(mktemp)"
    client_args=${CLIENT_ARGS[$example]:-${CLIENT_ARGS["default"]}}
    if ! timeout 20 bazel run ${example}_client -- $client_args &> $CLIENT_LOG; then
        fail "client failed to communicate with server
        got server log:
        $(cat $SERVER_LOG)
        ------------------
        got client log:
        $(cat $CLIENT_LOG)
        "
    else
        pass "client successfully communicated with server"
    fi

    # Check server log for expected output if expecting an
    # output
    if [ -n "${EXPECTED_SERVER_OUTPUT[$example]}" ]; then
        if ! grep -q "${EXPECTED_SERVER_OUTPUT[$example]}" $SERVER_LOG; then
            fail "server log missing output: ${EXPECTED_SERVER_OUTPUT[$example]}
            got server log:
            $(cat $SERVER_LOG)
            ---------------------
            got client log:
            $(cat $CLIENT_LOG)
            "
        else
            pass "server log contains expected output: ${EXPECTED_SERVER_OUTPUT[$example]}"
        fi
    fi


    # Check client log for expected output if expecting an
    # output
    if [ -n "${EXPECTED_CLIENT_OUTPUT[$example]}" ]; then
        if ! grep -q "${EXPECTED_CLIENT_OUTPUT[$example]}" $CLIENT_LOG; then
            fail "client log missing output: ${EXPECTED_CLIENT_OUTPUT[$example]}
            got server log:
            $(cat $SERVER_LOG)
            got client log:
            $(cat $CLIENT_LOG)
            "
        else
            pass "client log contains expected output: ${EXPECTED_CLIENT_OUTPUT[$example]}"
        fi
    fi
    clean
done
