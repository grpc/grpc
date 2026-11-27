#!/bin/bash
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

# Build portability tests with an updated submodule

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Run setup script in docker.
docker \
  run \
  --rm \
  -v "${PWD}:/var/grpc" \
  -e "RUN_TESTS_FLAGS" \
  -e "BAZEL_FLAGS" \
  -e GRPC_GENERATE_PROJECTS_SKIP_XDS_PROTOS="${GRPC_GENERATE_PROJECTS_SKIP_XDS_PROTOS:-1}" \
  --workdir=/var/grpc \
  $(cat "tools/dockerfile/test/cxx_debian12_x64.current_version") \
  tools/internal_ci/linux/grpc_setup_submodule_in_docker.sh

tools/run_tests/run_tests_matrix.py -f linux --exclude c sanity basictests_arm64 openssl dbg --inner_jobs 16 -j 2 --build_only
