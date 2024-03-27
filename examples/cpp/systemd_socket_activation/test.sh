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

PORT=3465

# Run this script as normal user

fail() {
    echo "FAIL: $@" >&2
}

pass() {
    echo "SUCCESS: $1"
}

setup() {
    local sd_socket="$1"
    local grpc_server="$2"
    local working_dir="$3"
    local bind_ipv6only="$4"
    local additional_listener="$5"
    local server_options="$6"

    cp -f ../../../bazel-bin/examples/cpp/systemd_socket_activation/server /tmp/greeter_server
    cp -f ../../../bazel-bin/examples/cpp/systemd_socket_activation/client /tmp/greeter_client

    mkdir -p ~/.config/systemd/user/

cat << EOF > ~/.config/systemd/user/sdsockact.service
[Service]
ExecStart=/tmp/greeter_server --listen="${grpc_server}" ${server_options}
WorkingDirectory=${working_dir}
EOF

cat << EOF > ~/.config/systemd/user/sdsockact.socket
[Install]
WantedBy=sockets.target
[Socket]
SocketMode=600
BindIPv6Only=${bind_ipv6only}
ListenStream=${sd_socket}
EOF

    # tail ~/.config/systemd/user/sdsockact.socket ~/.config/systemd/user/sdsockact.service

    # append the additional listener if present
    if [[ "${additional_listener}" != "-" ]]
    then
        echo "ListenStream=${additional_listener}" >> ~/.config/systemd/user/sdsockact.socket
    fi

    # reload after adding units
    systemctl --user daemon-reload

    # starts the listening socket but not the service
    systemctl --user restart sdsockact.socket
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
    local sd_socket="${1}"
    local grpc_server="${2}"
    local grpc_client="${3}"
    local working_dir="${4}"
    local bind_ipv6only="${5}"
    local additional_listener="${6}"
    local server_options="${7}"

    # setup systemd service and socket
    setup ${sd_socket} ${grpc_server} ${working_dir} ${bind_ipv6only} ${additional_listener} ${server_options}

    cd ${working_dir}

    # run the client and trigger the server through socket activation
    TEST_COUNT=$((TEST_COUNT+1))

    echo "Client queries ${grpc_client}..."
    /tmp/greeter_client --target=${grpc_client} | grep "Hello"
    if [ $? -ne 0 ]; then
        FAIL_COUNT=$((FAIL_COUNT+1))
        fail "Response not received"
    else
        pass "Response received"
    fi

    cd - >/dev/null

    # print connections state for network sockets
    [[ "${grpc_server}" != *"unix"* ]] && ss -plant | awk "NR==1 || /:${PORT}/"

    # request server shutdown by sending SIGINT
    SERVICE_PID=$(systemctl --user show --property MainPID --value  sdsockact.service)
    [[ -n "${SERVICE_PID}" ]] && [[ ${SERVICE_PID} -ne 0 ]] && kill -s SIGINT ${SERVICE_PID}

    # cleanup by removing sytemd units
    teardown
}

# run test

track_test() {
    echo "==== $1 ===="
    $1
}

# tests unix socket

test_sd_unix_relative() {
    run_test "/tmp/server" "unix:server" "unix:server" "/tmp" "default" "-" "-"
}

test_sd_unix_relative_dot() {
    run_test "/tmp/server" "unix:./server" "unix:./server" "/tmp" "default" "-" "-"
}

test_sd_unix_absolute() {
    run_test "/tmp/server" "unix:/tmp/server" "unix:/tmp/server" "$(pwd)" "default" "-" "-"
}

test_sd_unix_absolute_scheme() {
    run_test "/tmp/server" "unix:///tmp/server" "unix:///tmp/server" "$(pwd)" "default" "-" "-"
}

test_sd_unix_abstract() {
    run_test "@test_unix_abstract" "unix-abstract:test_unix_abstract" "unix-abstract:test_unix_abstract" "$(pwd)" "default" "-" "-"
}

# tests ip

test_sd_loopback_ipv4_port() {
    run_test "127.0.0.1:${PORT}" "127.0.0.1:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "default" "-" "-"
}

test_sd_loopback_ipv6_dualstack_port() {
    run_test "[::1]:${PORT}" "[::1]:${PORT}" "[::1]:${PORT}" "$(pwd)" "both" "-" "-"

    # The address [::1] aka "IPv6 loopback" **even when dual-stack**,
    # can only bind to an IPv6 loopback interface, but *not* an IPv4 loopback.
    # This is in contrast to the [::] aka "IPv6 wildcard", which when dual-stack,
    # can actually bind to any IPv6 and IPv4 interface at the same time.
    # So below is an expected "FAIL by design", which is kept only for reference :
    # run_test "[::1]:${PORT}" "[::1]:${PORT}" "127.0.0.1:{PORT}" "$(pwd)" "both" "-" "-"
}

test_sd_loopback_ipv6_only_port() {
    run_test "[::1]:${PORT}" "[::1]:${PORT}" "[::1]:${PORT}" "$(pwd)" "ipv6-only" "-" "-"

    # If the client would try 127.0.0.1, systemd would *not* activate and run the server,
    # because systemd listening socket is ipv6 only. So if the client would try,
    # 127.0.0.1 would fail. And even if the server would actually be started, the client
    # connection would still not reach the server, because the listening socket is IPv6 only
    # So below is an expected "FAIL by design", which is kept only for reference :
    # run_test "[::1]:${PORT}" "[::1]:${PORT}" "127.0.0.1:{PORT}" "$(pwd)" "ipv6-only" "-" "-"
}

test_sd_wildcard_ipv4_expand_port() {
    run_test "127.0.0.1:${PORT}" "0.0.0.0:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "default" "-" "--expand-wildcard-addr"
}

test_sd_wildcard_ipv6_expand_dualstack_port() {
    run_test "127.0.0.1:${PORT}" "[::]:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "both" "-" "--expand-wildcard-addr"
    run_test "[::1]:${PORT}" "[::]:${PORT}" "[::1]:${PORT}" "$(pwd)" "both" "-" "--expand-wildcard-addr"
}

test_sd_wildcard_ipv6_expand_only_port() {
    run_test "127.0.0.1:${PORT}" "[::]:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "ipv6-only" "-" "--expand-wildcard-addr"
    run_test "[::1]:${PORT}" "[::]:${PORT}" "[::1]:${PORT}" "$(pwd)" "ipv6-only" "-" "--expand-wildcard-addr"
}

test_sd_wildcard_ipv4_port() {
    run_test "0.0.0.0:${PORT}" "0.0.0.0:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "default" "-" "-"
}

test_sd_wildcard_ipv6_dualstack_port() {
    run_test "[::]:${PORT}" "[::]:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "both" "-" "-"
    run_test "[::]:${PORT}" "[::]:${PORT}" "[::1]:${PORT}" "$(pwd)" "both" "-" "-"
}

test_sd_wildcard_ipv6_only_port_and_grpc_created_wildcard_ipv4() {
    run_test "[::]:${PORT}" "[::]:${PORT}" "[::1]:${PORT}" "$(pwd)" "ipv6-only" "-" "-"

    # If the client would try 127.0.0.1, systemd would *not* activate and run the server,
    # because systemd listening socket is ipv6 only. So if the client would try that
    # 127.0.0.1 would fail, even though the server *would* create a listener on that 127.0.0.1,
    # if it were indeed run, because ListenerContainerAddWildcardAddresses() tries to add
    # an ipv4 wildcard listener when the ipv6 wildcard returns an ipv6-only socket.
    # So below is an expected "FAIL by design", which is kept only for reference :
    # run_test "[::]:${PORT}" "[::]:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "ipv6-only" "-" "-"
}

test_sd_wildcard_ipv6_only_port_and_sd_wildcard_ipv4() {
    run_test "[::]:${PORT}" "[::]:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "ipv6-only" "0.0.0.0:${PORT}" "-"
    run_test "[::]:${PORT}" "[::]:${PORT}" "[::1]:${PORT}" "$(pwd)" "ipv6-only" "0.0.0.0:${PORT}" "-"
}

test_sd_localhost_ipv4() {
    # due to DNS resolution, gRPC will add two listeners : 127.0.0.1 and [::1]
    # only connections to 127.0.0.1 would trigger a activation via systemd,
    # and gRPC would use this preallocated socket as a listener. But due to resolution,
    # the gRPC created [::1] listener would only be available when the server is running,
    # but could never trigger an activation through Systemd, which does not know it.
    # Here the client IP will first be 127.0.0.1, as `localhost` the default system
    # behaviour is to resolved into IPv6 ([::1]) first, then revert to IPv4 if it fails.
    # So the client first tries to connect [::1] but it does not succeed, as System
    # activation only listens on IPv4 loopback address. When the system falls back to
    # IPv4, the connection succeeds (due to valid address for activation) and starts
    # the server. The server finds the preallocated IPv4 socket and gets the IPv4 client.
    # Ironically, due to gRPC DNS resolution, once the server is started, it adds
    # an IPv6 listener for `localhost`, so the next client connections *while the server
    # is running* would then actually arrive in IPv6, as most servers are "IPv6-first".
    run_test "127.0.0.1:${PORT}" "localhost:${PORT}" "localhost:${PORT}" "$(pwd)" "default" "-" "-"
}

test_sd_localhost_ipv6() {
    # due to DNS resolution, gRPC will add two listeners : 127.0.0.1 and [::1]
    # only connections to [::1] would trigger a activation via systemd,
    # and gRPC would use this preallocated socket as a listener. But due to resolution,
    # the gRPC creates another 127.0.0.1 listener, which would only be available
    # when the server is running, but could never trigger an activation through Systemd,
    # which does not know it.
    # So the system of the client first tries to connect [::1] and succeeds, as System
    # activation listens on IPv6 loopback address. The server starts and finds the
    # preallocated IPv6 socket and gets the IPv6 client. The server starts the other
    # listener for the other resolved address (127.0.0.1) which will be accessible to
    # IPv4 clients, but only while the server is running.
    run_test "[::1]:${PORT}" "localhost:${PORT}" "localhost:${PORT}" "$(pwd)" "default" "-" "-"
}

test_sd_localhost_dualstack() {
    # TODO(nipil): explain
    # TODO(nipil): detect and explain client source ip
    run_test "[::1]:${PORT}" "localhost:${PORT}" "127.0.0.1:${PORT}" "$(pwd)" "ipv6-only" "127.0.0.1:${PORT}" "-"
    run_test "[::1]:${PORT}" "localhost:${PORT}" "[::1]:${PORT}" "$(pwd)" "ipv6-only" "127.0.0.1:${PORT}" "-"
}

# main

echo "==== building ===="
bazel build --define=use_systemd=true //examples/cpp/systemd_socket_activation:all || {
    fail "Failed to build systemd_socket_activation"
    exit
}

track_test test_sd_unix_relative
track_test test_sd_unix_relative_dot
track_test test_sd_unix_absolute
track_test test_sd_unix_absolute_scheme

track_test test_sd_unix_abstract

track_test test_sd_loopback_ipv4_port
track_test test_sd_loopback_ipv6_dualstack_port
track_test test_sd_loopback_ipv6_only_port

track_test test_sd_wildcard_ipv4_port

track_test test_sd_wildcard_ipv6_dualstack_port
track_test test_sd_wildcard_ipv6_only_port_and_grpc_created_wildcard_ipv4
track_test test_sd_wildcard_ipv6_only_port_and_sd_wildcard_ipv4

track_test test_sd_wildcard_ipv4_expand_port
track_test test_sd_wildcard_ipv6_expand_dualstack_port
track_test test_sd_wildcard_ipv6_expand_only_port

track_test test_sd_localhost_ipv4
track_test test_sd_localhost_ipv6
track_test test_sd_localhost_dualstack

echo "==== ${FAIL_COUNT} tests failed, ${TEST_COUNT} tests run ===="

exit ${FAIL_COUNT}
