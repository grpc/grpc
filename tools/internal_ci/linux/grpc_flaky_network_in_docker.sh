#!/usr/bin/env bash
# Copyright 2019 The gRPC Authors
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
#
# Run the flaky network test
#
set -ex

# iptables is used to drop traffic between client and server
apt-get install -y iptables iproute2

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_flaky_network_test
bazel_flaky_network_test/bazel_wrapper test --test_output=all --test_timeout=1200 //test/cpp/end2end:flaky_network_test --test_env=GRPC_TRACE=http --test_env=GRPC_VERBOSITY=DEBUG
