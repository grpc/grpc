#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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

# install pre-requisites for gRPC C core build
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python3 python3-pip clang

python3 --version

cd grpc

# tests require port server to be running
python3 tools/run_tests/start_port_server.py

# test gRPC C/C++ with bazel
tools/bazel test --config=opt --test_output=errors --test_tag_filters=-no_linux,-no_arm64 --build_tag_filters=-no_linux,-no_arm64 --flaky_test_attempts=1 --runs_per_test=1 //test/...
