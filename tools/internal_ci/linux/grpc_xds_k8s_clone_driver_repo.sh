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

set -eo pipefail

# Test driver
readonly TEST_DRIVER_REPO_URL="https://github.com/${TEST_DRIVER_REPO_OWNER:-grpc}/grpc.git"
readonly TEST_DRIVER_BRANCH="${TEST_DRIVER_BRANCH:-master}"
readonly TEST_DRIVER_INSTALL_LIB_PATH="tools/internal_ci/linux/grpc_xds_k8s_install_test_driver.sh"

#######################################
# Ensure the source code of the test driver is present at TEST_DRIVER_REPO_DIR.
#
# If TEST_DRIVER_REPO_DIR is set, this method only confirms that this directory exists.
# In this case, TEST_DRIVER_BRANCH is not used.
#
# Otherwise, clone branch TEST_DRIVER_BRANCH of the test driver from TEST_DRIVER_REPO_URL repo to
# a temporary folder, and export the path in TEST_DRIVER_REPO_DIR global variable.
#
# Globals:
#   TEST_DRIVER_REPO_DIR: path to the repo containing the test driver.
#   TEST_DRIVER_REPO_URL: the repo with the source code of test driver to clone
#   TEST_DRIVER_BRANCH: (if the repo is cloned from TEST_DRIVER_REPO_URL) the branch to checkout
# Arguments:
#   None
# Outputs:
#   Writes the output of `git` command to stdout, stderr
#   Writes driver source code to $TEST_DRIVER_REPO_DIR
#######################################
clone_test_driver() {
  if [[ -d "${TEST_DRIVER_REPO_DIR}" ]]; then
    echo "Using existing driver directory: ${TEST_DRIVER_REPO_DIR}"
  else
    readonly TEST_DRIVER_REPO_DIR="$(mktemp -d)/xds-k8s-driver-repo"
    echo "Cloning driver to ${TEST_DRIVER_REPO_URL} branch ${TEST_DRIVER_BRANCH} to ${TEST_DRIVER_REPO_DIR}"
    git clone -b "${TEST_DRIVER_BRANCH}" --depth=1 "${TEST_DRIVER_REPO_URL}" "${TEST_DRIVER_REPO_DIR}"
  fi

  # Source the test driver script.
  # shellcheck source="${TEST_DRIVER_REPO_DIR}/${TEST_DRIVER_INSTALL_LIB_PATH}"
  source "${TEST_DRIVER_REPO_DIR}/${TEST_DRIVER_INSTALL_LIB_PATH}"
}
