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

# Enter the gRPC repo root
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

# Set up environment variables
PREBUILT_IMAGE_PREFIX="gcr.io/grpc-testing/e2etesting/pre_built_workers/"${KOKORO_BUILD_INITIATOR}
UNIQUE_IDENTIFIER=$(date +%Y%m%d%H%M%S)
ROOT_DIRECTORY_OF_DOCKERFILES="../test-infra/containers/pre_built_workers/"
GRPC_CORE_GITREF="$(git ls-remote https://github.com/grpc/grpc.git master | cut -c1-7)"
GRPC_GO_GITREF="$(git ls-remote https://github.com/grpc/grpc-go.git master | cut -c1-7)"
GRPC_JAVA_GITREF="$(git ls-remote https://github.com/grpc/grpc-java.git master | cut -c1-7)"


# Clone test-infra repository to one upper level directory than grpc
cd ..
git clone --recursive https://github.com/grpc/test-infra.git
cd grpc

# If there is a error within the function, will exit directly
deleteImages() {
  echo "an error has occurred after pushing the images, deleting images"
  go run ../test-infra/tools/delete_prebuilt_workers/delete_prebuilt_workers.go \
  -p $PREBUILT_IMAGE_PREFIX \
  -t $UNIQUE_IDENTIFIER
}
trap deleteImages EXIT

# Build and push prebuilt images for running tests
go run ../test-infra/tools/prepare_prebuilt_workers/prepare_prebuilt_workers.go \
  -l cxx:$GRPC_CORE_GITREF \
  -l csharp:$GRPC_CORE_GITREF \
  -l go:$GRPC_GO_GITREF \
  -l java:$GRPC_JAVA_GITREF \
  -l python:$GRPC_CORE_GITREF \
  -l ruby:$GRPC_CORE_GITREF \
  -p $PREBUILT_IMAGE_PREFIX \
  -t $UNIQUE_IDENTIFIER \
  -r $ROOT_DIRECTORY_OF_DOCKERFILES

# This is subject to change. Runs a single test and does not wait for the
# result.
tools/run_tests/performance/loadtest_config.py -l c++ -l go \
    -t ./tools/run_tests/performance/templates/loadtest_template_basic_all_languages.yaml \
    -s client_pool=workers-8core -s server_pool=workers-8core \
    -s big_query_table=e2e_benchmarks.experimental_results \
    -s timeout_seconds=900 \
    -s prebuilt_image_prefix=$PREBUILT_IMAGE_PREFIX \
    -s prebuilt_image_tag=$UNIQUE_IDENTIFIER \
    --prefix=$KOKORO_BUILD_INITIATOR -u $UNIQUE_IDENTIFIER \
    -r '(go_generic_sync_streaming_ping_pong_secure|go_protobuf_sync_unary_ping_pong_secure|cpp_protobuf_async_streaming_qps_unconstrained_secure)$' \
    -o ./loadtest_with_prebuilt_images.yaml

# Dump the contents of the loadtest_with_prebuilt_images.yaml (since
# loadtest_config.py doesn't list the scenarios that will be run).
cat ./loadtest_with_prebuilt_images.yaml

# The original version of the client is old, update to the latest release
# version v1.21.0.
kubectl version --client
curl -sSL -O https://dl.k8s.io/release/v1.21.0/bin/linux/amd64/kubectl
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
chmod +x kubectl
sudo mv kubectl $(which kubectl)
kubectl version --client

kubectl apply -f ./loadtest_with_prebuilt_images.yaml
