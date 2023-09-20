#!/bin/bash
# Copyright 2021 The gRPC authors.
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

# Build portability tests with an updated submodule

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Since grpc_bazel_distribtest kokoro job does not have different
# configuration files for master/PR, use a workaround to detect when running
# on pull request.
# TODO(jtattermusch): Once the grpc_bazel_distribtest.cfg kokoro
# is fully retired on both presubmit and master, simply get rid
# of the job configuration and this script.
if [ "${KOKORO_GITHUB_PULL_REQUEST_NUMBER}" != "" ]
then
  echo "Bazel distribtests have been migrated to tools/bazelify_tests"
  echo "and are running on bazel RBE. It is no longer necessary"
  echo "to run them on presubmit in a dedicated kokoro job."
  echo ""
  echo "Exiting the kokoro job since there is nothing to do."
  exit 0
fi

export DOCKERFILE_DIR=tools/dockerfile/test/bazel
export DOCKER_RUN_SCRIPT=test/distrib/bazel/run_bazel_distrib_test.sh
exec tools/run_tests/dockerize/build_and_run_docker.sh
