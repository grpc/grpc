#!/bin/sh
# Copyright 2017 gRPC authors.
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

set -ex
export GRPC_POLL_STRATEGY=$1
shift
blackhole_address_setting="$1"
if [ "$blackhole_address_setting" = "can_create" ]; then
  if [ "$GRPC_TEST_RUNNING_UNDER_RBE" = "1" ]; then
    # Tests on RBE run as root within individual docker containers,
    # and have the NET_ADMIN capability, so it's safe to modify the network
    # stack.
    export GRPC_TEST_LINUX_ONLY_BLACKHOLE_ADDRESS=can_create
  else
    echo "bug in build system: attempting to run a grpc_cc_test with rbe_linux_only_uses_blackholed_ipv6_address=True outside of bazel RBE"
    exit 1
  fi
fi
shift
"$@"
