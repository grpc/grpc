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
    LOAD_TEST_PREFIX=kokoro
fi
# Use the "official" BQ tables so that the measurements will show up in the
# "official" public dashboard.
BIGQUERY_TABLE_8CORE=e2e_benchmarks.ci_master_results_8core
BIGQUERY_TABLE_32CORE=e2e_benchmarks.ci_master_results_32core
# END differentiate experimental configuration from master configuration.
CLOUD_LOGGING_URL="https://source.cloud.google.com/results/invocations/${KOKORO_BUILD_ID}"
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etesting/pre_built_workers/${LOAD_TEST_PREFIX}"
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
GRPC_GO_GITREF="$(git ls-remote https://github.com/grpc/grpc-go.git master | cut -f1)"
GRPC_JAVA_GITREF="$(git ls-remote https://github.com/grpc/grpc-java.git master | cut -f1)"
# Kokoro jobs run on dedicated pools.
DRIVER_POOL=drivers-ci
WORKER_POOL_8CORE=workers-c2-8core-ci
# c2-standard-30 is the closest machine spec to 32 core there is
WORKER_POOL_32CORE=workers-c2-30core-ci

# Update go version.
TEST_INFRA_GOVERSION=go1.17.1
go get "golang.org/dl/${TEST_INFRA_GOVERSION}"
"${TEST_INFRA_GOVERSION}" download

# Fetch test-infra repository and build all tools.
# Note: Submodules are not required for tools build.
pushd ..
git clone --depth 1 https://github.com/grpc/test-infra.git
cd test-infra
git log -1 --oneline
make GOCMD="${TEST_INFRA_GOVERSION}" all-tools
popd

# Build test configurations.
buildConfigs() {
    local -r pool="$1"
    local -r table="$2"
    shift 2
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_all_languages.yaml \
        -s driver_pool="${DRIVER_POOL}" -s driver_image= \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s big_query_table="${table}" -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -a ci_buildNumber="${KOKORO_BUILD_NUMBER}" \
        -a ci_buildUrl="${CLOUD_LOGGING_URL}" \
        -a ci_jobName="${KOKORO_JOB_NAME}" \
        -a ci_gitCommit="${GRPC_GITREF}" \
        -a ci_gitActualCommit="${KOKORO_GIT_COMMIT}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" \
        -a pool="${pool}" --category=scalable \
        --allow_client_language=c++ --allow_server_language=c++ \
        -o "./loadtest_with_prebuilt_workers_${pool}.yaml"
}

buildConfigs "${WORKER_POOL_8CORE}" "${BIGQUERY_TABLE_8CORE}" -l c++ -l csharp -l go -l java -l php7 -l php7_protobuf_c -l python -l ruby
buildConfigs "${WORKER_POOL_32CORE}" "${BIGQUERY_TABLE_32CORE}" -l c++ -l csharp -l go -l java

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
    -l "csharp:${GRPC_CORE_GITREF}" \
    -l "go:${GRPC_GO_GITREF}" \
    -l "java:${GRPC_JAVA_GITREF}" \
    -l "php7:${GRPC_CORE_GITREF}" \
    -l "python:${GRPC_CORE_GITREF}" \
    -l "ruby:${GRPC_CORE_GITREF}" \
    -p "${PREBUILT_IMAGE_PREFIX}" \
    -t "${UNIQUE_IDENTIFIER}" \
    -r "${ROOT_DIRECTORY_OF_DOCKERFILES}"

# Run tests.
time ../test-infra/bin/runner \
    -i "../grpc/loadtest_with_prebuilt_workers_${WORKER_POOL_8CORE}.yaml" \
    -i "../grpc/loadtest_with_prebuilt_workers_${WORKER_POOL_32CORE}.yaml" \
    -delete-successful-tests \
    -c "${WORKER_POOL_8CORE}:2" -c "${WORKER_POOL_32CORE}:2" \
    -o "runner/sponge_log.xml"
