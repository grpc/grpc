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
# GKE Cluster
readonly GKE_CLUSTER_NAME="interop-test-psm-sec-v2-us-central1-a"
readonly GKE_CLUSTER_ZONE="us-central1-a"

## xDS test server/client Docker images
readonly IMAGE_REPO="gcr.io/grpc-testing/xds-interop"
readonly SERVER_LANG="cpp go java"
readonly CLIENT_LANG="cpp go java"
readonly VERSION_TAG="v1.40.x"

#######################################
# Executes the test case
# Globals:
#   TEST_DRIVER_FLAGFILE: Relative path to test driver flagfile
#   KUBE_CONTEXT: The name of kubectl context with GKE cluster access
#   TEST_XML_OUTPUT_DIR: Output directory for the test xUnit XML report
#   SERVER_IMAGE_NAME: Test server Docker image name
#   CLIENT_IMAGE_NAME: Test client Docker image name
#   GIT_COMMIT: SHA-1 of git commit being built
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
  set -x
  python -m "tests.security_test" \
    --flagfile="${TEST_DRIVER_FLAGFILE}" \
    --kube_context="${KUBE_CONTEXT}" \
    --server_image="${server_image_name}" \
    --client_image="${client_image_name}" \
    --xml_output_file="${TEST_XML_OUTPUT_DIR}/${tag}/${clang}-${slang}/sponge_log.xml" \
    --force_cleanup \
    --nocheck_local_certs
  set +x
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
  # shellcheck source=tools/internal_ci/linux/grpc_xds_k8s_install_test_driver.sh
  source "${script_dir}/grpc_xds_k8s_install_test_driver.sh"
  set -x
  if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
    kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"
    cd "${TEST_DRIVER_FULL_DIR}"
  else
    local_setup_test_driver "${script_dir}"
    cd "${SRC_DIR}/${TEST_DRIVER_PATH}"
  fi

  local failed_tests=0
  # Run tests
  for TAG in ${VERSION_TAG}
  do
    for CLANG in ${CLIENT_LANG}
    do
    for SLANG in ${SERVER_LANG}
    do
      if [ "${CLANG}" != "${SLANG}" ]; then
        run_test "${TAG}" "${SLANG}" "${CLANG}" || (( failed_tests++ ))
      fi
    done
    echo "Failed test suites: ${failed_tests}"
    done
  done
  if (( failed_tests > 0 )); then
    exit 1
  fi
}

main "$@"
