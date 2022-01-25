#!/usr/bin/env bash
# Copyright 2021 gRPC authors.
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

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

cat rbellevi_id_rsa.pub >> ~/.ssh/authorized_keys

tail -f /dev/null

export DOCKERFILE_DIR=tools/dockerfile/test/binder_transport_apk
export DOCKER_RUN_SCRIPT=$BAZEL_SCRIPT
exec tools/run_tests/dockerize/build_and_run_docker.sh
