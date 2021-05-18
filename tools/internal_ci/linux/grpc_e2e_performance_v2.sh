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
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# This is to ensure we can push and pull images from gcr.io. We do not
# necessarily need it to run load tests, but will need it when we employ
# pre-built images in the optimization.
gcloud auth configure-docker

# Connect to benchmarks-prod cluster.
gcloud config set project grpc-testing
gcloud container clusters get-credentials benchmarks-prod \
    --zone us-central1-b --project grpc-testing

# List pods that may be left over from a previous run.
kubectl get pods | grep -v Completed

# Set up environment variables.
# BEGIN differentiate experimental configuration from master configuration.
LOAD_TEST_PREFIX="${KOKORO_BUILD_INITIATOR}"
if [[ "${KOKORO_BUILD_INITIATOR}" == kokoro ]]; then
    LOAD_TEST_PREFIX=kokoro-test
fi
BIGQUERY_TABLE_8CORE=e2e_benchmarks.experimental_results
BIGQUERY_TABLE_32CORE=e2e_benchmarks.experimental_results_32core
# END differentiate experimental configuration from master configuration.
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etesting/pre_built_workers/${LOAD_TEST_PREFIX}"
UNIQUE_IDENTIFIER="$(date +%Y%m%d%H%M%S)"
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
# Prebuilt workers for core languages are always built from grpc/grpc.
if [[ "${KOKORO_GITHUB_COMMIT_URL%/*}" == "https://github.com/grpc/grpc/commit" ]]; then
    GRPC_CORE_GITREF="${KOKORO_GIT_COMMIT}"
else
    GRPC_CORE_GITREF="$(git ls-remote https://github.com/grpc/grpc.git master | cut -f1)"
fi
GRPC_GO_GITREF="$(git ls-remote https://github.com/grpc/grpc-go.git master | cut -f1)"
GRPC_JAVA_GITREF="$(git ls-remote https://github.com/grpc/grpc-java.git master | cut -f1)"

# Clone test-infra repository to one upper level directory than grpc.
pushd ..
git clone --recursive https://github.com/grpc/test-infra.git
cd test-infra
go build -o bin/runner cmd/runner/main.go
go build -o bin/prepare_prebuilt_workers tools/prepare_prebuilt_workers/prepare_prebuilt_workers.go
go build -o bin/delete_prebuilt_workers tools/delete_prebuilt_workers/delete_prebuilt_workers.go
popd

# Build test configurations.
buildConfigs() {
    local pool="$1"
    local table="$2"
    shift 2
    tools/run_tests/performance/loadtest_config.py "$@" \
        -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_all_languages.yaml \
        -s client_pool="${pool}" -s server_pool="${pool}" \
        -s big_query_table="${table}" \
        -s timeout_seconds=900 \
        -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
        -s prebuilt_image_tag="${UNIQUE_IDENTIFIER}" \
        --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${pool}" \
        -a pool="${pool}" --category=scalable \
        --allow_client_language=c++ --allow_server_language=c++ \
        -o "./loadtest_with_prebuilt_workers_${pool}.yaml"
}

buildConfigs workers-8core "${BIGQUERY_TABLE_8CORE}" -l c++ -l csharp -l go -l java -l python -l ruby

buildConfigs workers-32core "${BIGQUERY_TABLE_32CORE}" -l c++ -l csharp -l go -l java

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
    -l "python:${GRPC_CORE_GITREF}" \
    -l "ruby:${GRPC_CORE_GITREF}" \
    -p "${PREBUILT_IMAGE_PREFIX}" \
    -t "${UNIQUE_IDENTIFIER}" \
    -r "${ROOT_DIRECTORY_OF_DOCKERFILES}"

# Run tests.
time ../test-infra/bin/runner \
    -i ../grpc/loadtest_with_prebuilt_workers_workers-8core.yaml \
    -i ../grpc/loadtest_with_prebuilt_workers_workers-32core.yaml \
    -c workers-8core:2 -c workers-32core:2
