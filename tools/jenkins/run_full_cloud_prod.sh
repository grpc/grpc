#!/usr/bin/env bash
# Copyright 2016 gRPC authors.
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
# This script is invoked by Jenkins and runs interop test suite.
set -ex

export LANG=en_US.UTF-8

# Enter the gRPC repo root
cd $(dirname $0)/../..

tools/run_tests/run_interop_tests.py \
    -l all \
    --cloud_to_prod \
    --cloud_to_prod_auth \
    --prod_servers default cloud_gateway gateway_v4 cloud_gateway_v4 \
    --use_docker -t -j 12 $@ || true
