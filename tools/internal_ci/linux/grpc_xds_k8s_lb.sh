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
## xDS test client Docker images
readonly SERVER_IMAGE_NAME="gcr.io/grpc-testing/xds-interop/cpp-server"
readonly CLIENT_IMAGE_NAME="gcr.io/grpc-testing/xds-interop/cpp-client"
readonly FORCE_IMAGE_BUILD="${FORCE_IMAGE_BUILD:-0}"
readonly BUILD_APP_PATH="interop-testing/build/install/grpc-interop-testing"

#######################################
# Builds test app Docker images and pushes them to GCR
# Globals:
#   BUILD_APP_PATH
#   SERVER_IMAGE_NAME: Test server Docker image name
#   CLIENT_IMAGE_NAME: Test client Docker image name
#   GIT_COMMIT: SHA-1 of git commit being built
# Arguments:
#   None
# Outputs:
#   Writes the output of `gcloud builds submit` to stdout, stderr
#######################################
build_test_app_docker_images() {
  echo "Building C++ xDS interop test app Docker images"
  docker build -f "${SRC_DIR}/tools/dockerfile/interoptest/grpc_interop_cxx_xds/Dockerfile.xds_client" -t "${CLIENT_IMAGE_NAME}:${GIT_COMMIT}" "${SRC_DIR}"
  docker build -f "${SRC_DIR}/tools/dockerfile/interoptest/grpc_interop_cxx_xds/Dockerfile.xds_server" -t "${SERVER_IMAGE_NAME}:${GIT_COMMIT}" "${SRC_DIR}"
  gcloud -q auth configure-docker
  docker push "${CLIENT_IMAGE_NAME}:${GIT_COMMIT}"
  docker push "${SERVER_IMAGE_NAME}:${GIT_COMMIT}"
  if is_version_branch "${TESTING_VERSION}"; then
    tag_and_push_docker_image "${CLIENT_IMAGE_NAME}" "${GIT_COMMIT}" "${TESTING_VERSION}"
    tag_and_push_docker_image "${SERVER_IMAGE_NAME}" "${GIT_COMMIT}" "${TESTING_VERSION}"
  fi
}

#######################################
# Builds test app and its docker images unless they already exist
# Globals:
#   SERVER_IMAGE_NAME: Test server Docker image name
#   CLIENT_IMAGE_NAME: Test client Docker image name
#   GIT_COMMIT: SHA-1 of git commit being built
#   FORCE_IMAGE_BUILD
# Arguments:
#   None
# Outputs:
#   Writes the output to stdout, stderr
#######################################
build_docker_images_if_needed() {
  # Check if images already exist
  server_tags="$(gcloud_gcr_list_image_tags "${SERVER_IMAGE_NAME}" "${GIT_COMMIT}")"
  printf "Server image: %s:%s\n" "${SERVER_IMAGE_NAME}" "${GIT_COMMIT}"
  echo "${server_tags:-Server image not found}"

  client_tags="$(gcloud_gcr_list_image_tags "${CLIENT_IMAGE_NAME}" "${GIT_COMMIT}")"
  printf "Client image: %s:%s\n" "${CLIENT_IMAGE_NAME}" "${GIT_COMMIT}"
  echo "${client_tags:-Client image not found}"

  # Build if any of the images are missing, or FORCE_IMAGE_BUILD=1
  if [[ "${FORCE_IMAGE_BUILD}" == "1" || -z "${server_tags}" || -z "${client_tags}" ]]; then
    build_test_app_docker_images
  else
    echo "Skipping C++ test app build"
  fi
}

#######################################
# Executes the test case
# Globals:
#   TEST_DRIVER_FLAGFILE: Relative path to test driver flagfile
#   KUBE_CONTEXT: The name of kubectl context with GKE cluster access
#   SECONDARY_KUBE_CONTEXT: The name of kubectl context with secondary GKE cluster access, if any
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
  local test_name="${1:?Usage: run_test test_name}"
  local out_dir="${TEST_XML_OUTPUT_DIR}/${test_name}"
  mkdir -pv "${out_dir}"
  set -x
  python3 -m "tests.${test_name}" \
    --flagfile="${TEST_DRIVER_FLAGFILE}" \
    --kube_context="${KUBE_CONTEXT}" \
    --secondary_kube_context="${SECONDARY_KUBE_CONTEXT}" \
    --server_image="${SERVER_IMAGE_NAME}:${GIT_COMMIT}" \
    --client_image="${CLIENT_IMAGE_NAME}:${GIT_COMMIT}" \
    --testing_version="${TESTING_VERSION}" \
    --force_cleanup \
    --collect_app_logs \
    --log_dir="${out_dir}" \
    --xml_output_file="${out_dir}/sponge_log.xml" \
    ${@:2} \
    |& tee "${out_dir}/sponge_log.log"
}

run_alpha_test() {
  local test_name=$1
  run_test ${test_name} \
    --compute_api_version="v1alpha"
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
#   SECONDARY_KUBE_CONTEXT: Populated with name of kubectl context with secondary GKE cluster access, if any
# Arguments:
#   None
# Outputs:
#   Writes the output of test execution to stdout, stderr
#######################################
main() {
  local script_dir
  script_dir="$(dirname "$0")"

  # Source the test captured from the master branch.
  echo "Sourcing test driver install captured from: ${TEST_DRIVER_INSTALL_SCRIPT_URL}"
  source /dev/stdin <<< "$(curl -s "${TEST_DRIVER_INSTALL_SCRIPT_URL}")"

  activate_gke_cluster GKE_CLUSTER_PSM_LB
  activate_secondary_gke_cluster GKE_CLUSTER_PSM_LB

  set -x
  if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
    kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"
  else
    local_setup_test_driver "${script_dir}"
  fi
  build_docker_images_if_needed

  # Run tests
  cd "${TEST_DRIVER_FULL_DIR}"
  local failed_tests=0
  run_alpha_test subsetting_test || (( ++failed_tests ))
  test_suites=("api_listener_test" "change_backend_service_test" "failover_test" "remove_neg_test" "round_robin_test" "affinity_test" "outlier_detection_test" "custom_lb_test")
  for test in "${test_suites[@]}"; do
    run_test $test || (( ++failed_tests ))
  done
  echo "Failed test suites: ${failed_tests}"
}

main "$@"
