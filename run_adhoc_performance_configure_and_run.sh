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
cd "$(dirname "$0")"

# Connect to benchmarks-prod2 cluster.
#gcloud config set project grpc-testing
gcloud container clusters get-credentials benchmarks-prod2 \
    --zone us-central1-b --project grpc-testing

# Set up environment variables.
LOAD_TEST_PREFIX="$USER"
UNIQUE_IDENTIFIER="$(date +%Y%m%d%H%M%S)"
PREBUILT_IMAGE_TAG="20210930093820"

BIGQUERY_TABLE=e2e_benchmarks.jtattermusch_experiment_20211019
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etesting/pre_built_workers/${LOAD_TEST_PREFIX}"
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
# Head of the workspace checked out by Kokoro.
GRPC_GITREF="$(git show --format="%H" --no-patch)"
GRPC_CORE_GITREF=${GRPC_GITREF}

# Kokoro jobs run on dedicated pools.
DRIVER_POOL=drivers
WORKER_POOL="workers-c2-8core"

tools/run_tests/performance/loadtest_config.py -l c++ -r 'cpp_protobuf_sync_unary_ping_pong_secure' \
    -t ./tools/run_tests/performance/templates/loadtest_template_prebuilt_all_languages.yaml \
    -s driver_pool="${DRIVER_POOL}" -s driver_image= \
    -s client_pool="${WORKER_POOL}" \
    -s server_pool="${WORKER_POOL}" \
    -s big_query_table="${BIGQUERY_TABLE}" -s timeout_seconds=900 \
    -s prebuilt_image_prefix="${PREBUILT_IMAGE_PREFIX}" \
    -s prebuilt_image_tag="${PREBUILT_IMAGE_TAG}" \
    --prefix="${LOAD_TEST_PREFIX}" -u "${UNIQUE_IDENTIFIER}" -u "${WORKER_POOL}" \
    -a pool="${WORKER_POOL}" --category=scalable \
    --allow_client_language=c++ --allow_server_language=c++ \
    --runs_per_test 10 \
    -o "./loadtest_with_prebuilt_workers.yaml"

# # Create reports directories.
mkdir -p "runner/${WORKER_POOL}"

# Run tests.
time ../test-infra/bin/runner \
    -i "../grpc/loadtest_with_prebuilt_workers.yaml" \
    -c "${WORKER_POOL}:1" \
    -o "runner/sponge_log.xml"
