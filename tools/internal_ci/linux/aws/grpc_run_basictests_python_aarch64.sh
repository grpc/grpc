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

# install python3.6 and pip
sudo apt install -y python3 python3-pip
python3 --version

cd grpc

git submodule update --init

# build and test python (currently we only test with python3.7, but that's ok since our aarch64 testing resources are limited)
tools/run_tests/run_tests.py -l python --compiler python3.7 -c opt -t -x run_tests/python_linux_opt_native/sponge_log.xml --report_suite_name python_linux_opt_native --report_multi_target || FAILED=true

if [ "$FAILED" != "" ]
then
  exit 1
fi
