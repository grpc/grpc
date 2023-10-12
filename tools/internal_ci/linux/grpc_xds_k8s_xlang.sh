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

# Constants
readonly GITHUB_REPOSITORY_NAME="grpc"
readonly TEST_DRIVER_INSTALL_SCRIPT_URL="https://raw.githubusercontent.com/${TEST_DRIVER_REPO_OWNER:-grpc}/grpc/${TEST_DRIVER_BRANCH:-master}/tools/internal_ci/linux/grpc_xds_k8s_install_test_driver.sh"
## xDS test server/client Docker images
readonly SERVER_LANGS="cpp go java"
readonly CLIENT_LANGS="cpp go java"
readonly MAIN_BRANCH="${MAIN_BRANCH:-master}"

#######################################
# Main function: provision software necessary to execute tests, and run them
# Globals:
#   KOKORO_ARTIFACTS_DIR
#   GITHUB_REPOSITORY_NAME
#   SRC_DIR: Populated with absolute path to the source repo
#   TEST_DRIVER_REPO_DIR: Populated with the path to the repo containing
#                         the test driver
#   TEST_DRIVER_FULL_DIR: Populated with the path to the test driver source code
#   TEST_DRIVER_FLAGFILE: Populated with relative path to test driver flagfile
#   TEST_XML_OUTPUT_DIR: Populated with the path to test xUnit XML report
#   GIT_ORIGIN_URL: Populated with the origin URL of git repo used for the build
#   GIT_COMMIT: Populated with the SHA-1 of git commit being built
#   GIT_COMMIT_SHORT: Populated with the short SHA-1 of git commit being built
#   KUBE_CONTEXT: Populated with name of kubectl context with GKE cluster access
# Arguments:
#   None
# Outputs:
#   Writes the output of test execution to stdout, stderr
#######################################
main() {
  local script_dir
  script_dir="${PWD}/$(dirname "$0")"

  # Source the test driver from the master branch.
  echo "Sourcing test driver install script from: ${TEST_DRIVER_INSTALL_SCRIPT_URL}"
  source /dev/stdin <<< "$(curl -s "${TEST_DRIVER_INSTALL_SCRIPT_URL}")"

  activate_gke_cluster GKE_CLUSTER_PSM_SECURITY

  set -x
  if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
    kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"
    if [ "${TESTING_VERSION}" != "master" ]; then
      echo "Skipping cross lang testing for non-master branch ${TESTING_VERSION}"
      exit 0
    fi
    cd "${TEST_DRIVER_FULL_DIR}"
  else
    local_setup_test_driver "${script_dir}"
    cd "${SRC_DIR}/${TEST_DRIVER_PATH}"
  fi

  source "${script_dir}/grpc_xds_k8s_run_xtest.sh"

  local failed_tests=0
  local successful_string
  local failed_string
  LATEST_BRANCH=$(find_latest_branch "${LATEST_BRANCH}")
  OLDEST_BRANCH=$(find_oldest_branch "${OLDEST_BRANCH}" "${LATEST_BRANCH}")
  # Run cross lang tests: for given cross lang versions
  XLANG_VERSIONS="${MAIN_BRANCH} ${LATEST_BRANCH} ${OLDEST_BRANCH}"
  declare -A FIXED_VERSION_NAMES=( ["${MAIN_BRANCH}"]="${MAIN_BRANCH}" ["${LATEST_BRANCH}"]="latest" ["${OLDEST_BRANCH}"]="oldest")
  for VERSION in ${XLANG_VERSIONS}
  do
    for CLIENT_LANG in ${CLIENT_LANGS}
    do
    for SERVER_LANG in ${SERVER_LANGS}
    do
      if [ "${CLIENT_LANG}" != "${SERVER_LANG}" ]; then
        FIXED="${FIXED_VERSION_NAMES[${VERSION}]}"
        if run_test "${CLIENT_LANG}" "${VERSION}" "${SERVER_LANG}" "${VERSION}" "${FIXED}" "${FIXED}"; then
          successful_string="${successful_string} ${VERSION}/${CLIENT_LANG}-${SERVER_LANG}"
        else
          failed_tests=$((failed_tests+1))
          failed_string="${failed_string} ${VERSION}/${CLIENT_LANG}-${SERVER_LANG}"
        fi
      fi
    done
    echo "Failed test suites: ${failed_tests}"
    done
  done
  set +x
  echo "Failed test suites list: ${failed_string}"
  echo "Successful test suites list: ${successful_string}"
}

main "$@"
