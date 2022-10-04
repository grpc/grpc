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

# Connect to benchmarks-prod2 cluster.
gcloud config set project grpc-testing
gcloud container clusters get-credentials psm-benchmarks-performance \
    --zone us-central1-b --project grpc-testing

# Set up environment variables.
LOAD_TEST_PREFIX="wanlin-cpu-test"

# END differentiate experimental configuration from master configuration.
CLOUD_LOGGING_URL="https://source.cloud.google.com/results/invocations/${KOKORO_BUILD_ID}"
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etest/prebuilt/${LOAD_TEST_PREFIX}"
UNIQUE_IDENTIFIER="$(date +%Y%m%d%H%M%S)"

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
DRIVER_POOL=drivers
WORKER_POOL_8CORE=workers
# Prefix for log URLs in cnsviewer.
LOG_URL_PREFIX="http://cnsviewer/placer/prod/home/kokoro-dedicated/build_artifacts/${KOKORO_BUILD_ARTIFACTS_SUBDIR}/github/grpc/"

# Update go version.
TEST_INFRA_GOVERSION=go1.17.1
go get "golang.org/dl/${TEST_INFRA_GOVERSION}"
"${TEST_INFRA_GOVERSION}" download

# PSM tests related ENV
PSM_IMAGE_PREFIX=gcr.io/grpc-testing/e2etest/runtime
PSM_IMAGE_TAG=v1.4.1

# Build psm test configurations.
psmBuildConfigs() {
    local -r pool="$1"
    local -r proxy_type="$2"

    shift 2
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_psm_"${proxy_type}"_prebuilt_all_languages.yaml \
        -s driver_pool="${DRIVER_POOL}" -s driver_image= \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        -s psm_image_prefix="${PSM_IMAGE_PREFIX}" \
        -s psm_image_tag="${PSM_IMAGE_TAG}" \
        -s big_query_table= \
        -a ci_buildUrl="${CLOUD_LOGGING_URL}" \
        -a ci_gitCommit="${GRPC_GITREF}" \
        -a ci_gitCommit_java="${GRPC_JAVA_GITREF}" \
        -a enablePrometheus=true \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" -u "${proxy_type}"\
        -a pool="${pool}" \
        --category=psm \
        --allow_client_language=c++ --allow_server_language=c++ \
        -o "psm_${proxy_type}_loadtest_with_prebuilt_workers_${pool}.yaml"
}

psmBuildConfigs "${WORKER_POOL_8CORE}" proxied -a queue="${WORKER_POOL_8CORE}-proxied"  -l c++ --client_channels=8 --server_threads=16 --offered_loads 100 300 500 700 900 1000 1500 2000 2500 4000 6000 8000 10000 12000 14000 16000 18000 20000 22000 24000 26000 28000 30000

psmBuildConfigs "${WORKER_POOL_8CORE}" proxyless -a queue="${WORKER_POOL_8CORE}-proxyless" -l c++ --client_channels=8 --server_threads=16 --offered_loads 100 300 500 700 900 1000 1500 2000 2500 4000 6000 8000 10000 12000 14000 16000 18000 20000 22000 24000 26000 28000 30000

# Build regular test configurations.
buildConfigs() {
    local -r pool="$1"
    shift 1
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_all_languages.yaml \
        -s driver_pool="${DRIVER_POOL}" -s driver_image= \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        -s big_query_table= \
        -a ci_buildUrl="${CLOUD_LOGGING_URL}" \
        -a ci_gitCommit="${GRPC_GITREF}" \
        -a ci_gitCommit_java="${GRPC_JAVA_GITREF}" \
        -a enablePrometheus=true \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" \
        -a pool="${pool}" --category=psm \
        --allow_client_language=c++ --allow_server_language=c++ \
        -o "regular_loadtest_with_prebuilt_workers_${pool}.yaml"
}

buildConfigs "${WORKER_POOL_8CORE}" -a queue="${WORKER_POOL_8CORE}-regular" -l c++ --client_channels=8 --server_threads=16 --offered_loads 100 300 500 700 900 1000 1500 2000 2500 4000 6000 8000 10000 12000 14000 16000 18000 20000 22000 24000 26000 28000 30000

# Run tests.
time ../test-infra/bin/runner \
    -i "psm_proxied_loadtest_with_prebuilt_workers_${WORKER_POOL_8CORE}.yaml" \
    -log-url-prefix "${LOG_URL_PREFIX}" \
    -annotation-key queue \
    -polling-interval 5s \
    -delete-successful-tests \
    -c "${WORKER_POOL_8CORE}-proxied:1" \
    -o "runner/sponge_log.xml"
