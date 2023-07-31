#!/usr/bin/env bash
# Copyright 2023 The gRPC Authors
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

# Purpose: Run the C++ "dashboard" benchmarks for a set of gRPC-core experiments.
#
# To run the benchmarks, add your experiment to the set below.
GRPC_EXPERIMENTS=("event_engine_listener" "work_stealing")

# Enter the gRPC repo root.
cd "$(dirname "$0")/../../.."

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Environment variables to select repos and branches for various repos.
# You can edit these lines if you want to run from a fork.
GRPC_CORE_REPO=grpc/grpc
GRPC_CORE_GITREF=master
TEST_INFRA_REPO=grpc/test-infra
TEST_INFRA_GITREF=master

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
    LOAD_TEST_PREFIX=kokoro
fi
# Use the "official" BQ tables so that the measurements will show up in the
# "official" public dashboard.
BIGQUERY_TABLE_8CORE=e2e_benchmark_cxx_experiments.results_8core
BIGQUERY_TABLE_32CORE=e2e_benchmark_cxx_experiments.results_32core
# END differentiate experimental configuration from master configuration.
CLOUD_LOGGING_URL="https://source.cloud.google.com/results/invocations/${KOKORO_BUILD_ID}"
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etest/prebuilt/cxx_experiment/${LOAD_TEST_PREFIX}"
UNIQUE_IDENTIFIER="cxx-experiment-$(date +%Y%m%d%H%M%S)"
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
# Head of the workspace checked out by Kokoro.
GRPC_COMMIT="$(git show --format="%H" --no-patch)"
# Prebuilt workers for core languages are always built from grpc/grpc.
if [[ "${KOKORO_GITHUB_COMMIT_URL%/*}" == "https://github.com/grpc/grpc/commit" ]]; then
    GRPC_CORE_COMMIT="${KOKORO_GIT_COMMIT}"
else
    GRPC_CORE_COMMIT="$(git ls-remote -h "https://github.com/${GRPC_CORE_REPO}.git" "${GRPC_CORE_GITREF}" | cut -f1)"
fi

# Kokoro jobs run on dedicated pools.
DRIVER_POOL=drivers-ci
WORKER_POOL_8CORE=workers-c2-8core-ci
# c2-standard-30 is the closest machine spec to 32 core there is
WORKER_POOL_32CORE=workers-c2-30core-ci
# Prefix for log URLs in cnsviewer.
LOG_URL_PREFIX="http://cnsviewer/placer/prod/home/kokoro-dedicated/build_artifacts/${KOKORO_BUILD_ARTIFACTS_SUBDIR}/github/grpc/"

# Clone test-infra repository and build all tools.
mkdir ../test-infra
pushd ../test-infra
git clone "https://github.com/${TEST_INFRA_REPO}.git" .
git checkout "${TEST_INFRA_GITREF}"
make all-tools
popd

declare -a loadtest_files=()

# Build test configurations.
buildConfigs() {
    local -r pool="$1"
    local -r table="$2"
    local -r experiment="$3"
    shift 3
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_cxx_experiments.yaml \
        -s driver_pool="${DRIVER_POOL}" -s driver_image= \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s big_query_table="${table}_${experiment}" -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        -s grpc_experiment="${experiment}" \
        -a ci_buildNumber="${KOKORO_BUILD_NUMBER}" \
        -a ci_buildUrl="${CLOUD_LOGGING_URL}" \
        -a ci_jobName="${KOKORO_JOB_NAME}" \
        -a ci_gitCommit="${GRPC_COMMIT}" \
        -a ci_gitCommit_core="${GRPC_CORE_COMMIT}" \
        -a ci_gitActualCommit="${KOKORO_GIT_COMMIT}" \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" \
        -a pool="${pool}" --category=dashboard \
        --allow_client_language=c++ --allow_server_language=c++ \
        --allow_server_language=node \
        -o "loadtest_with_prebuilt_workers_${pool}_${experiment}.yaml"

    loadtest_files+=(-i "loadtest_with_prebuilt_workers_${pool}_${experiment}.yaml")
}

for experiment in "${GRPC_EXPERIMENTS[@]}"; do
    buildConfigs "${WORKER_POOL_8CORE}" "${BIGQUERY_TABLE_8CORE}" "${experiment}" -l c++
    buildConfigs "${WORKER_POOL_32CORE}" "${BIGQUERY_TABLE_32CORE}" "${experiment}" -l c++
done

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
    -l "cxx:${GRPC_CORE_REPO}:${GRPC_CORE_COMMIT}" \
    -p "${PREBUILT_IMAGE_PREFIX}" \
    -t "${UNIQUE_IDENTIFIER}" \
    -r "${ROOT_DIRECTORY_OF_DOCKERFILES}"

# Run tests.
../test-infra/bin/runner \
    ${loadtest_files[@]} \
    -log-url-prefix "${LOG_URL_PREFIX}" \
    -polling-interval 5s \
    -delete-successful-tests \
    -c "${WORKER_POOL_8CORE}:2" -c "${WORKER_POOL_32CORE}:2" \
    -o "runner/sponge_log.xml"
