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
readonly IMAGE_REPO="gcr.io/grpc-testing/xds-interop"
readonly SERVER_LANG="cpp go java"
readonly CLIENT_LANG="cpp go java"
readonly VERSION_TAG="master v1.49.x"

#######################################
# Executes the test case
# Globals:
#   TEST_DRIVER_FLAGFILE: Relative path to test driver flagfile
#   KUBE_CONTEXT: The name of kubectl context with GKE cluster access
#   TEST_XML_OUTPUT_DIR: Output directory for the test xUnit XML report
#   SERVER_IMAGE_NAME: Test server Docker image name
#   CLIENT_IMAGE_NAME: Test client Docker image name
#   GIT_COMMIT: SHA-1 of git commit being built
#   TESTING_VERSION: version branch under test: used by the framework to determine the supported PSM
#                    features.
# Arguments:
#   Test case name
# Outputs:
#   Writes the output of test execution to stdout, stderr
#   Test xUnit report to ${TEST_XML_OUTPUT_DIR}/${test_name}/sponge_log.xml
#######################################
run_test() {
  # Test driver usage:
  # https://github.com/grpc/grpc/tree/master/tools/run_tests/xds_k8s_test_driver#basic-usage
  local tag="${1:?Usage: run_test tag server_lang client_lang}"
  local slang="${2:?Usage: run_test tag server_lang client_lang}"
  local clang="${3:?Usage: run_test tag server_lang client_lang}"
  local server_image_name="${IMAGE_REPO}/${slang}-server:${tag}"
  local client_image_name="${IMAGE_REPO}/${clang}-client:${tag}"
  # TODO(sanjaypujare): skip test if image not found (by using gcloud_gcr_list_image_tags)
  local out_dir="${TEST_XML_OUTPUT_DIR}/${tag}/${clang}-${slang}"
  mkdir -pv "${out_dir}"
  set -x
  python -m "tests.security_test" \
    --flagfile="${TEST_DRIVER_FLAGFILE}" \
    --kube_context="${KUBE_CONTEXT}" \
    --server_image="${server_image_name}" \
    --client_image="${client_image_name}" \
    --testing_version="${TESTING_VERSION}" \
    --nocheck_local_certs \
    --force_cleanup \
    --collect_app_logs \
    --log_dir="${out_dir}" \
    --xml_output_file="${out_dir}/sponge_log.xml" \
    |& tee "${out_dir}/sponge_log.log"
}

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
  script_dir="$(dirname "$0")"

  # Source the test driver from the master branch.
  echo "Sourcing test driver install script from: ${TEST_DRIVER_INSTALL_SCRIPT_URL}"
  source /dev/stdin <<< "$(curl -s "${TEST_DRIVER_INSTALL_SCRIPT_URL}")"

  activate_gke_cluster GKE_CLUSTER_PSM_SECURITY

  set -x
  if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
    kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"
    cd "${TEST_DRIVER_FULL_DIR}"
  else
    local_setup_test_driver "${script_dir}"
    cd "${SRC_DIR}/${TEST_DRIVER_PATH}"
  fi

  local failed_tests=0
  local successful_string
  local failed_string
  # Run tests
  for TAG in ${VERSION_TAG}
  do
    for CLANG in ${CLIENT_LANG}
    do
    for SLANG in ${SERVER_LANG}
    do
      if [ "${CLANG}" != "${SLANG}" ]; then
        if run_test "${TAG}" "${SLANG}" "${CLANG}"; then
          successful_string="${successful_string} ${TAG}/${CLANG}-${SLANG}"
        else
          failed_tests=$((failed_tests+1))
          failed_string="${failed_string} ${TAG}/${CLANG}-${SLANG}"
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
