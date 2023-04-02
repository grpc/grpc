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
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip clang
sudo pip install six

# install gRPC Ruby pre-requisites
sudo apt install -y php7.2-cli php7.2-dev
sudo apt install -y composer phpunit
sudo apt install -y php-bcmath  # required by protobuf PHP, is it really needed for gRPC?

cd grpc

git submodule update --init

# build and test php
tools/run_tests/run_tests.py -l php7 -c opt -t -x run_tests/php7_linux_aarch64_opt_native/sponge_log.xml --report_suite_name php7_linux_aarch64_opt_native --report_multi_target
