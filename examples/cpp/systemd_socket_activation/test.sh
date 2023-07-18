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

# Run this script as root

clean() {
    echo "Cleaning..."
    systemctl stop sdsockact.socket
    systemctl stop sdsockact.service
    systemctl daemon-reload
    rm /tmp/greeter_server
    rm /tmp/greeter_client
    rm /etc/systemd/system/sdsockact.service
    rm /etc/systemd/system/sdsockact.socket
}

fail() {
    clean
    echo "FAIL: $@" >&2
    exit 1
}

pass() {
    echo "SUCCESS: $1"
}

bazel build --define=use_systemd=true //examples/cpp/systemd_socket_activation:all || fail "Failed to build sd_sock_act"
cp ../../../../bazel-bin/examples/cpp/systemd_socket_activation/server /tmp/greeter_server
cp ../../../../bazel-bin/examples/cpp/systemd_socket_activation/client /tmp/greeter_client

cat << EOF > /etc/systemd/system/sdsockact.service
[Service]
ExecStart=/tmp/greeter_server
EOF

cat << EOF > /etc/systemd/system/sdsockact.socket
[Socket]
ListenStream=/tmp/server
ReusePort=true

[Install]
WantedBy=sockets.target
EOF

systemctl daemon-reload
systemctl enable sdsockact.socket
systemctl start sdsockact.socket

pushd /tmp
./greeter_client | grep "Hello"
if [ $? -ne 0 ]; then
    popd
    fail "Response not received"
fi

popd
pass "Response received"
clean
