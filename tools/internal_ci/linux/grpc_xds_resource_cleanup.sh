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

set -ex

# consts
readonly GITHUB_REPOSITORY_NAME="grpc"
readonly TEST_DRIVER_INSTALL_SCRIPT_URL="https://raw.githubusercontent.com/${TEST_DRIVER_REPO_OWNER:-grpc}/grpc/${TEST_DRIVER_BRANCH:-master}/tools/internal_ci/linux/grpc_xds_k8s_install_test_driver.sh"

cd "$(dirname "$0")/../../.."

# Source the test driver from the master branch.
echo "Sourcing test driver install script from: ${TEST_DRIVER_INSTALL_SCRIPT_URL}"
source /dev/stdin <<< "$(curl -s "${TEST_DRIVER_INSTALL_SCRIPT_URL}")"
activate_gke_cluster GKE_CLUSTER_PSM_SECURITY
kokoro_setup_test_driver "${GITHUB_REPOSITORY_NAME}"

cd "${TEST_DRIVER_FULL_DIR}"

# flag resource_prefix is required by the gke test framework, but doesn't
# matter for the cleanup script.
python3 -m bin.cleanup.cleanup \
    --project=grpc-testing \
    --network=default-vpc \
    --kube_context="${KUBE_CONTEXT}" \
    --gcp_service_account=xds-k8s-interop-tests@grpc-testing.iam.gserviceaccount.com \
    --resource_prefix='required-but-does-not-matter' \
    --td_bootstrap_image='required-but-does-not-matter' --server_image='required-but-does-not-matter' --client_image='required-but-does-not-matter'

# The BASIC cluster is used by url-map tests. Only cleaning the GKE client
# namespaces, which won't provide much value in debugging. The keep hours is
# reduced to 6.
activate_gke_cluster GKE_CLUSTER_PSM_BASIC
# Invoking the get-crednetials directly, because the
# gcloud_get_cluster_credentials re-sets readonly Bash variables, which is nice
# safety mechanism to keep.
gcloud container clusters get-credentials "${GKE_CLUSTER_NAME}" --zone "${GKE_CLUSTER_ZONE}"
TARGET_KUBE_CONTEXT="$(kubectl config current-context)"
python3 -m bin.cleanup.namespace \
    --project=grpc-testing \
    --network=default-vpc \
    --keep_hours=6 \
    --kube_context="${TARGET_KUBE_CONTEXT}" \
    --gcp_service_account=xds-k8s-interop-tests@grpc-testing.iam.gserviceaccount.com \
    --resource_prefix='required-but-does-not-matter' \
    --td_bootstrap_image='required-but-does-not-matter' --server_image='required-but-does-not-matter' --client_image='required-but-does-not-matter'

# The PSM_LB cluster is used by k8s_lb tests. Only cleaning the GKE client
# namespaces, which won't provide much value in debugging. The keep hours is
# reduced to 6.
activate_gke_cluster GKE_CLUSTER_PSM_LB
gcloud container clusters get-credentials "${GKE_CLUSTER_NAME}" --zone "${GKE_CLUSTER_ZONE}"
TARGET_KUBE_CONTEXT="$(kubectl config current-context)"
python3 -m bin.cleanup.namespace \
    --project=grpc-testing \
    --network=default-vpc \
    --keep_hours=6 \
    --kube_context="${TARGET_KUBE_CONTEXT}" \
    --gcp_service_account=xds-k8s-interop-tests@grpc-testing.iam.gserviceaccount.com \
    --resource_prefix='required-but-does-not-matter' \
    --td_bootstrap_image='required-but-does-not-matter' --server_image='required-but-does-not-matter' --client_image='required-but-does-not-matter'
