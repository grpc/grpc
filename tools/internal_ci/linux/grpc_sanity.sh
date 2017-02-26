#!/bin/bash
# Copyright 2017, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

git submodule update --init

# download base docker image from dockerhub
export DOCKERHUB_ORGANIZATION=grpctesting
tools/run_tests/run_tests.py -l sanity -c opt -t -x sponge_log.xml --use_docker --report_suite_name sanity_linux_opt
