#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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

cleanup::activate_cluster() {
  activate_gke_cluster "$1"
  gcloud container clusters get-credentials "${GKE_CLUSTER_NAME}" \
    --zone "${GKE_CLUSTER_ZONE}"
  CLEANUP_KUBE_CONTEXT="$(kubectl config current-context)"
}

cleanup::activate_secondary_cluster_as_primary() {
  activate_secondary_gke_cluster "$1"
  GKE_CLUSTER_NAME="${SECONDARY_GKE_CLUSTER_NAME}"
  GKE_CLUSTER_ZONE="${SECONDARY_GKE_CLUSTER_ZONE}"
  gcloud container clusters get-credentials "${GKE_CLUSTER_NAME}" \
    --zone "${GKE_CLUSTER_ZONE}"
  CLEANUP_KUBE_CONTEXT="$(kubectl config current-context)"
}

cleanup::job::cleanup_td() {
  cleanup::run_clean "$1" --mode=td
}

#######################################
# The PSM_LB cluster is used by k8s_lb tests.
# The keep hours is reduced to 6.
#######################################
cleanup::job::cleanup_cluster_lb_primary() {
  cleanup::activate_cluster GKE_CLUSTER_PSM_LB
  cleanup::run_clean "$1" --mode=k8s --keep_hours=6
}

#######################################
# Secondary PSM_LB cluster is used by k8s_lb tests.
# The keep hours is reduced to 6.
#######################################
cleanup::job::cleanup_cluster_lb_secondary() {
  cleanup::activate_secondary_cluster_as_primary GKE_CLUSTER_PSM_LB
  cleanup::run_clean "$1" --mode=k8s --secondary --keep_hours=6
}

#######################################
# The BASIC cluster is used by url-map tests. Only cleaning the xds client
# namespaces; the xds server namespaces are shared.
# The keep hours is reduced to 6.
#######################################
cleanup::job::cleanup_cluster_url_map() {
  cleanup::activate_cluster GKE_CLUSTER_PSM_BASIC
  cleanup::run_clean "$1" --mode=k8s --keep_hours=6
}

#######################################
# The SECURITY cluster is used by the security and authz test suites.
#######################################
cleanup::job::cleanup_cluster_security() {
  cleanup::activate_cluster GKE_CLUSTER_PSM_SECURITY
  cleanup::run_clean "$1" --mode=k8s --keep_hours=6
}

#######################################
# Set common variables for the cleanup script.
#######################################
cleanup::run_clean() {
  local job_name="${1:?Usage: cleanup::run_clean job_name}"
  local out_dir="${TEST_XML_OUTPUT_DIR}/${job_name}"
  mkdir -pv "${out_dir}"
  python3 -m bin.cleanup.cleanup \
    --project=grpc-testing \
    --network=default-vpc \
    --color_style=ansi16 \
    --gcp_service_account=xds-k8s-interop-tests@grpc-testing.iam.gserviceaccount.com \
    --kube_context="${CLEANUP_KUBE_CONTEXT:-unset}" \
    "${@:2}" \
    |& tee "${out_dir}/sponge_log.log"
}

#######################################
# Main function: provision software necessary to execute the cleanup tasks;
# run them, and report the status.
#######################################
main() {
  local script_dir
  script_dir="$(dirname "$0")"

  # Source the test captured from the master branch.
  echo "Sourcing test driver install captured from: ${TEST_DRIVER_INSTALL_SCRIPT_URL}"
  source /dev/stdin <<< "$(curl -s "${TEST_DRIVER_INSTALL_SCRIPT_URL}")"
  set +x

  # Valid cluster variables needed for the automatic driver setup.
  activate_gke_cluster GKE_CLUSTER_PSM_BASIC
  kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"

  # Run tests
  cd "${TEST_DRIVER_FULL_DIR}"
  local failed_jobs=0
  declare -a cleanup_jobs
  cleanup_jobs=(
    "cleanup_td"
    "cleanup_cluster_lb_primary"
    "cleanup_cluster_lb_secondary"
    "cleanup_cluster_security"
    "cleanup_cluster_url_map"
  )
  for job_name in "${cleanup_jobs[@]}"; do
    echo "-------------------- Starting job ${job_name} --------------------"
    set -x
    "cleanup::job::${job_name}" "${job_name}" || (( ++failed_jobs ))
    set +x
    echo "-------------------- Finished job ${job_name} --------------------"
  done
  echo "Failed job suites: ${failed_jobs}"
  if (( failed_jobs > 0 )); then
    exit 1
  fi
}

main "$@"
