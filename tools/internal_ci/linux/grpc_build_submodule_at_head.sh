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

# Submodule name is passed as the RUN_TESTS_FLAGS variable
SUBMODULE_NAME="${RUN_TESTS_FLAGS}"

# Name of branch to checkout is passed as BAZEL_FLAGS variable
# If unset, "master" is used by default.
SUBMODULE_BRANCH_NAME="${BAZEL_FLAGS:-master}"

# Update submodule to be tested at HEAD
(cd "third_party/${SUBMODULE_NAME}" && git fetch origin && git checkout "origin/${SUBMODULE_BRANCH_NAME}")

echo "This suite tests whether gRPC HEAD builds with HEAD of submodule '${SUBMODULE_NAME}'"
echo "If a test breaks, either"
echo "1) some change in the grpc repository has caused the failure"
echo "2) some change that was just merged in the submodule head has caused the failure."

echo ""
echo "submodule '${SUBMODULE_NAME}' is at commit: $(cd third_party/${SUBMODULE_NAME}; git rev-parse --verify HEAD)"

if [ "${SUBMODULE_NAME}" == "abseil-cpp" ]
then
  src/abseil-cpp/preprocessed_builds.yaml.gen.py
fi

tools/buildgen/generate_projects.sh

if [ "${SUBMODULE_NAME}" == "protobuf" ]
then
  tools/distrib/python/make_grpcio_tools.py
fi

# commit so that changes are passed to Docker
git -c user.name='foo' -c user.email='foo@google.com' commit -a -m 'Update submodule' --allow-empty

tools/run_tests/run_tests_matrix.py -f linux --inner_jobs 8 -j 4 --internal_ci --build_only
