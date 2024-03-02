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
    exit 1
}

pass() {
    echo "SUCCESS: $1"
}

setup_before() {
    echo "Building..."
    bazel build --define=use_systemd=true //examples/cpp/systemd_socket_activation:all || fail "Failed to build sd_sock_act"

    echo "Copying executables..."
    cp ../../../bazel-bin/examples/cpp/systemd_socket_activation/server /tmp/greeter_server
    cp ../../../bazel-bin/examples/cpp/systemd_socket_activation/client /tmp/greeter_client

    echo "Configuring systemd..."
    mkdir -p ~/.config/systemd/user/

cat << EOF > ~/.config/systemd/user/sdsockact.service
[Service]
ExecStart=/tmp/greeter_server
EOF

cat << EOF > ~/.config/systemd/user/sdsockact.socket
[Socket]
ListenStream=/tmp/server
ReusePort=true

[Install]
WantedBy=sockets.target
EOF

    # reload after adding units
    systemctl --user daemon-reload

    ## starting at boot is not necessary for testing
    # systemctl --user enable sdsockact.socket
}

teardown_after() {
    echo "Cleaning executables..."
    rm -f /tmp/greeter_server
    rm -f /tmp/greeter_client
    echo "Cleaning remaining socket..."
    rm -f /tmp/server
    echo "Cleaning systemd units..."
    rm -f ~/.config/systemd/user/sdsockact.service
    rm -f ~/.config/systemd/user/sdsockact.socket

    # reload after removing units
    systemctl --user daemon-reload
}

trap teardown_after EXIT

setup() {
    systemctl --user start sdsockact.socket
}

teardown() {
    systemctl --user stop sdsockact.socket
    systemctl --user stop sdsockact.service
}

setup_before

setup
echo "Testing..."
/tmp/greeter_client | grep "Hello"
RESULT=$?
teardown

if [ ${RESULT} -ne 0 ]; then
    fail "Response not received"
fi
pass "Response received"

# teardown is called upon exit
