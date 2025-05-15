#!/usr/bin/env bash
# Copyright 2025 gRPC authors.
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

set -x
set +e

apt-get update && apt-get install -y libcap2-bin

# Enter the gRPC repo root
cd $(dirname $0)/../../..

echo "Current dir ${PWD}"

pushd "examples/cpp" || exit 1

SERVER_PID=

clean () {
  echo "CLeaning server ${SERVER_PID}"
  if [[ -n $SERVER_PID ]]; then
      kill -9 $SERVER_PID || true
  fi
  SERVER_PID=
}

fail () {
    echo "$(tput setaf 1) $1 $(tput sgr 0)"
    if [[ -n $SERVER_PID ]] ; then
      kill -9 $SERVER_PID || true
    fi
    SERVER_PID=
    exit 1
}

pass () {
    echo "$(tput setaf 2) $1 $(tput sgr 0)"
}

# "server", "client" suffix will be added in loop 
EXAMPLES=(
    "helloworld:greeter_callback_"
    "helloworld:greeter_async_"
    "helloworld:greeter_"
    "auth:ssl_"
    "cancellation:"
    "compression:compression_"
    "deadline:"
    "error_details:greeter_"
    "error_handling:greeter_"
    "flow_control:server_flow_control_"
    "flow_control:client_flow_control_"
    "generic_api:greeter_"
    "health:health_"
    "interceptors:keyvaluestore_"
    "keepalive:greeter_callback_"
    "load_balancing:lb_"
    "metadata:metadata_"
    "multiplex:multiplex_"
    "retry:"
    "route_guide:route_guide_callback_"
)

declare -A SERVER_ARGS=(
    ["default"]=""
)

declare -A CLIENT_ARGS=(
    ["default"]=""
)

declare -A EXPECTED_SERVER_OUTPUT=(
    ["default"]="Server listening"
    ["unix_abstract_sockets:"]="" 
)

declare -A EXPECTED_CLIENT_OUTPUT=(
    ["default"]="Greeter received: Hello world"
    ["cancellation:"]="Count 9 : Count 9 Ack"
    ["compression:compression_"]="Greeter received: Hello world world world"
    ["deadline:"]="\[Exceeds propagated deadline\] wanted = 4, got = 4"
    ["error_details:greeter_"]="Quota: subject=name: World description=Limit one greeting per person"
    ["error_handling:greeter_"]="Ok. ReplyMessage=Hello World"
    ["flow_control:client_flow_control_"]="Done"
    ["flow_control:server_flow_control_"]="Done reading"
    ["generic_api:greeter_"]="Ok. ReplyMessage=Hello gRPC"
    ["health:health_"]="After second call: status: SERVING"
    ["interceptors:keyvaluestore_"]="key4 found in map"
    ["metadata:metadata_"]="Client received trailing metadata from server: trailing metadata value"
    ["multiplex:multiplex_"]="Found feature: Feature: latitude: 50, longitude: 100"
    ["route_guide:route_guide_callback_"]="Got message First message at 0, 0"
    ["unix_abstract_sockets:"]="Received: arst"
)

for example in "${EXAMPLES[@]}"; do
    echo "$(tput setaf 4) testing: ${example} $(tput sgr 0)"

    # Build server
    if ! bazel build  ${example}server; then
        fail "failed to build server"
    else
        pass "successfully built server"
    fi

    # Start server
    SERVER_LOG="$(mktemp)"
    server_args=${SERVER_ARGS[$example]:-${SERVER_ARGS["default"]}}
    bazel run ${example}server -- $server_args &> $SERVER_LOG  &
    SERVER_PID=$!
    echo "Server Pid : ${SERVER_PID}"

    sleep 5

    # Build client
    if ! bazel build  ${example}client; then
        fail "failed to build client"
    else
        pass "successfully built client"
    fi

    # Start client
    CLIENT_LOG="$(mktemp)"
    client_args=${CLIENT_ARGS[$example]:-${CLIENT_ARGS["default"]}}
    if ! timeout 120 bazel run ${example}client -- $client_args &> $CLIENT_LOG; then
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

popd # go back to root

# For linux we use secure_getenv to read env variable
# this return null if linux capabilites are added on the executable
# For customer using linux capability , they need to define "--define GRPC_FORCE_UNSECURE_GETENV=1"
# to read env variables.
# enable log
export GRPC_TRACE=http
# Build using the define to force "getenv" instead of "secure_getenv"
bazel build  --define GRPC_FORCE_UNSECURE_GETENV=1 //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
setcap "cap_net_admin+ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
output_log=$(./bazel-bin/examples/cpp/helloworld/greeter_callback_client 2>&1)
# check if logs got enabled
if [[ ! "$output_log" =~ "gRPC Tracers:" ]]; then
    fail "Fail to read env variable with linux capability"
fi
# Build without the define , this will use secure_getenv
bazel build  //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
setcap "cap_net_admin+ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
output_log=$(./bazel-bin/examples/cpp/helloworld/greeter_callback_client 2>&1)
# We should not see log get enabled as secure_getenv will return null in this case
if [[ "$output_log" =~ "gRPC Tracers:" ]]; then
    fail "Able to read env variable with linux capability set"
fi
