#!/usr/bin/env bash
# Copyright 2022 The gRPC Authors
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

# Enter the gRPC repo root.
cd "$(dirname "$0")/../../.."

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# This is to ensure we can push and pull images from gcr.io. We do not
# necessarily need it to run load tests, but will need it when we employ
# pre-built images in the optimization.
gcloud auth configure-docker

# Connect to benchmarks-prod2 cluster.
gcloud config set project grpc-testing
gcloud container clusters get-credentials benchmarks-prod2 \
    --zone us-central1-b --project grpc-testing

# Set up environment variables.
LOAD_TEST_PREFIX="${KOKORO_BUILD_INITIATOR}"
# BEGIN differentiate experimental configuration from master configuration.
if [[ "${KOKORO_BUILD_INITIATOR%%-*}" == kokoro ]]; then
    LOAD_TEST_PREFIX=kokoro-test
fi
BIGQUERY_TABLE_8CORE=e2e_benchmarks.experimental_results
BIGQUERY_TABLE_32CORE=e2e_benchmarks.experimental_results_32core
# END differentiate experimental configuration from master configuration.
CLOUD_LOGGING_URL="https://source.cloud.google.com/results/invocations/${KOKORO_BUILD_ID}"
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etest/prebuilt/${LOAD_TEST_PREFIX}"
UNIQUE_IDENTIFIER="$(date +%Y%m%d%H%M%S)"
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
# Head of the workspace checked out by Kokoro.
GRPC_GITREF="$(git show --format="%H" --no-patch)"
# Prebuilt workers for core languages are always built from grpc/grpc.
if [[ "${KOKORO_GITHUB_COMMIT_URL%/*}" == "https://github.com/grpc/grpc/commit" ]]; then
    GRPC_CORE_GITREF="${KOKORO_GIT_COMMIT}"
else
    GRPC_CORE_GITREF="$(git ls-remote https://github.com/grpc/grpc.git master | cut -f1)"
fi
GRPC_JAVA_GITREF="$(git ls-remote https://github.com/grpc/grpc-java.git master | cut -f1)"
# Kokoro jobs run on dedicated pools.
DRIVER_POOL=drivers-ci
WORKER_POOL_8CORE=workers-c2-8core-ci
# c2-standard-30 is the closest machine spec to 32 core there is
WORKER_POOL_32CORE=workers-c2-30core-ci
# Prefix for log URLs in cnsviewer.
LOG_URL_PREFIX="http://cnsviewer/placer/prod/home/kokoro-dedicated/build_artifacts/${KOKORO_BUILD_ARTIFACTS_SUBDIR}/github/grpc/"

# Update go version.
TEST_INFRA_GOVERSION=go1.17.1
go get "golang.org/dl/${TEST_INFRA_GOVERSION}"
"${TEST_INFRA_GOVERSION}" download

# Clone test-infra repository and build all tools.
pushd ..
git clone https://github.com/grpc/test-infra.git
cd test-infra
# Tools are built from HEAD.
git checkout --detach
make GOCMD="${TEST_INFRA_GOVERSION}" all-tools

# Find latest release tag of test-infra.
git fetch --all --tags
TEST_INFRA_VERSION=$(git describe --tags "$(git rev-list --tags --max-count=1)")
popd

# PSM tests related ENV
PSM_PROXIED_TEST=proxied
PSM_PROXYLESS_TEST=proxyless
PSM_IMAGE_PREFIX=gcr.io/grpc-testing/e2etest/runtime
PSM_IMAGE_TAG=${TEST_INFRA_VERSION}
PSM_QUEUE_NAME_KEY=queue
PSM_8CORE_PROXYLESS_QUEUE_NAME="${WORKER_POOL_8CORE}-${PSM_PROXYLESS_TEST}"
PSM_8CORE_PROXIED_QUEUE_NAME="${WORKER_POOL_8CORE}-${PSM_PROXIED_TEST}"
PSM_32CORE_PROXYLESS_QUEUE_NAME="${WORKER_POOL_32CORE}-${PSM_PROXYLESS_TEST}"
PSM_32CORE_PROXIED_QUEUE_NAME="${WORKER_POOL_32CORE}-${PSM_PROXIED_TEST}"

# Build test configurations.
buildConfigs() {
    local -r pool="$1"
    local -r table="$2"
    local -r uniquifier="$3"

    shift 3
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_psm_"${uniquifier}"_prebuilt_all_languages.yaml \
        -s driver_pool="${DRIVER_POOL}" -s driver_image= \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s big_query_table="${table}" -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        -s psm_image_prefix="${PSM_IMAGE_PREFIX}" \
        -s psm_image_tag="${PSM_IMAGE_TAG}" \
        -a ci_buildNumber="${KOKORO_BUILD_NUMBER}" \
        -a ci_buildUrl="${CLOUD_LOGGING_URL}" \
        -a ci_jobName="${KOKORO_JOB_NAME}" \
        -a ci_gitCommit="${GRPC_GITREF}" \
        -a ci_gitCommit_java="${GRPC_JAVA_GITREF}" \
        -a ci_gitActualCommit="${KOKORO_GIT_COMMIT}" \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" -u "${uniquifier}"\
        -a pool="${pool}" \
        --category=psm \
        --allow_client_language=c++ --allow_server_language=c++ \
        -o "psm_${uniquifier}_loadtest_with_prebuilt_workers_${pool}.yaml"
}

buildConfigs "${WORKER_POOL_8CORE}" "${BIGQUERY_TABLE_8CORE}" "${PSM_PROXIED_TEST}"  -a "${PSM_QUEUE_NAME_KEY}"="${PSM_8CORE_PROXIED_QUEUE_NAME}" --client_channels=8 --server_threads=16 --offered_loads 5000 6000 -l c++ -l java
buildConfigs "${WORKER_POOL_8CORE}" "${BIGQUERY_TABLE_8CORE}" "${PSM_PROXYLESS_TEST}" -a "${PSM_QUEUE_NAME_KEY}"="${PSM_8CORE_PROXYLESS_QUEUE_NAME}" --client_channels=8 --server_threads=16 --offered_loads 5000 6000 -l c++ -l java
buildConfigs "${WORKER_POOL_32CORE}" "${BIGQUERY_TABLE_32CORE}" "${PSM_PROXIED_TEST}" -a "${PSM_QUEUE_NAME_KEY}"="${PSM_32CORE_PROXIED_QUEUE_NAME}" --client_channels=32 --server_threads=64 --offered_loads 5000 6000 -l c++ -l java
buildConfigs "${WORKER_POOL_32CORE}" "${BIGQUERY_TABLE_32CORE}" "${PSM_PROXYLESS_TEST}" -a "${PSM_QUEUE_NAME_KEY}"="${PSM_32CORE_PROXYLESS_QUEUE_NAME}" --client_channels=32 --server_threads=64 --offered_loads 5000 6000 -l c++ -l java

# Delete prebuilt images on exit.
deleteImages() {
    echo "deleting images on exit"
    ../test-infra/bin/delete_prebuilt_workers \
    -p "${PREBUILT_IMAGE_PREFIX}" \
    -t "${UNIQUE_IDENTIFIER}"
}
trap deleteImages EXIT

# Build and push prebuilt images for running tests.
time ../test-infra/bin/prepare_prebuilt_workers \
    -l "cxx:${GRPC_CORE_GITREF}" \
    -l "java:${GRPC_JAVA_GITREF}" \
    -p "${PREBUILT_IMAGE_PREFIX}" \
    -t "${UNIQUE_IDENTIFIER}" \
    -r "${ROOT_DIRECTORY_OF_DOCKERFILES}"

# Run tests.
time ../test-infra/bin/runner \
    -i "psm_${PSM_PROXIED_TEST}_loadtest_with_prebuilt_workers_${WORKER_POOL_8CORE}.yaml" \
    -i "psm_${PSM_PROXYLESS_TEST}_loadtest_with_prebuilt_workers_${WORKER_POOL_8CORE}.yaml" \
    -i "psm_${PSM_PROXIED_TEST}_loadtest_with_prebuilt_workers_${WORKER_POOL_32CORE}.yaml" \
    -i "psm_${PSM_PROXYLESS_TEST}_loadtest_with_prebuilt_workers_${WORKER_POOL_32CORE}.yaml" \
    -log-url-prefix "${LOG_URL_PREFIX}" \
    -polling-interval 5s \
    -delete-successful-tests \
    -annotation-key ${PSM_QUEUE_NAME_KEY} \
    -c "${PSM_8CORE_PROXIED_QUEUE_NAME}:1" \
    -c "${PSM_8CORE_PROXYLESS_QUEUE_NAME}:1" \
    -c "${PSM_32CORE_PROXIED_QUEUE_NAME}:1" \
    -c "${PSM_32CORE_PROXYLESS_QUEUE_NAME}:1" \
    -o "runner/sponge_log.xml"
