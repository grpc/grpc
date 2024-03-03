#!/usr/bin/env bash
# Copyright 2022 gRPC authors.
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

# Run this script as normal user

fail() {
    echo "FAIL: $@" >&2
}

pass() {
    echo "SUCCESS: $1"
}

setup() {
    local sd_socket="$1" grpc_target="$2" working_dir="$3"

    cp ../../../bazel-bin/examples/cpp/systemd_socket_activation/server /tmp/greeter_server
    cp ../../../bazel-bin/examples/cpp/systemd_socket_activation/client /tmp/greeter_client

    mkdir -p ~/.config/systemd/user/

cat << EOF > ~/.config/systemd/user/sdsockact.service
[Service]
ExecStart=/tmp/greeter_server --listen="${grpc_target}"
WorkingDirectory=${working_dir}
EOF

cat << EOF > ~/.config/systemd/user/sdsockact.socket
[Socket]
ListenStream=${sd_socket}
ReusePort=true
SocketMode=600

[Install]
WantedBy=sockets.target
EOF

    # reload after adding units
    systemctl --user daemon-reload

    # starts the listening socket but not the service
    systemctl --user restart sdsockact.socket

    ## starting at boot is not necessary for testing
    # systemctl --user enable sdsockact.socket
}

teardown() {
    systemctl --user stop sdsockact.socket
    systemctl --user stop sdsockact.service
    rm -f /tmp/greeter_server
    rm -f /tmp/greeter_client
    rm -f /tmp/server
    rm -f ~/.config/systemd/user/sdsockact.service
    rm -f ~/.config/systemd/user/sdsockact.socket
    # reload after removing units
    systemctl --user daemon-reload
}

TEST_COUNT=0
FAIL_COUNT=0

run_test() {
    local sd_socket="${1}" grpc_target="${2}" working_dir="${3}"

    setup ${sd_socket} ${grpc_target} ${working_dir}
    cd ${working_dir}

    TEST_COUNT=$((TEST_COUNT+1))
    /tmp/greeter_client --target=${grpc_target} | grep "Hello"
    if [ $? -ne 0 ]; then
        FAIL_COUNT=$((FAIL_COUNT+1))
        fail "Response not received"
    else
        pass "Response received"
    fi

    cd - >/dev/null
    teardown
}

# tests

test_unix_relative() {
    echo "==== test_unix_relative ===="
    run_test "/tmp/server" "unix:server" "/tmp"
}

test_unix_relative_dot() {
    echo "==== test_unix_relative ===="
    run_test "/tmp/server" "unix:./server" "/tmp"
}

test_unix_absolute() {
    echo "==== test_unix_absolute ===="
    run_test "/tmp/server" "unix:/tmp/server" "$(pwd)"
}

test_unix_absolute_scheme() {
    echo "==== test_unix_absolute_scheme ===="
    run_test "/tmp/server" "unix:///tmp/server" "$(pwd)"
}

test_unix_abstract() {
    echo "==== test_unix_abstract ===="
    run_test "@test_unix_abstract" "unix-abstract:test_unix_abstract" "$(pwd)"
}

# main

echo "==== building ===="
bazel build --define=use_systemd=true //examples/cpp/systemd_socket_activation:all || {
    fail "Failed to build systemd_socket_activation"
    exit
}

test_unix_relative
test_unix_relative_dot
test_unix_absolute
test_unix_absolute_scheme
test_unix_abstract

echo "==== ${FAIL_COUNT} tests failed, ${TEST_COUNT} tests run ===="

exit ${FAIL_COUNT}
