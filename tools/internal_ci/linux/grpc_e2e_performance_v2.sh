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

# This is to insure we can push and pull images from gcr.io, we do not
# necessarily need it to run load tests, but would need it when we employ
# pre-built images in the optimization.
gcloud auth configure-docker

# Connect to benchmarks-prod cluster
gcloud config set project grpc-testing
gcloud container clusters get-credentials benchmarks-prod \
    --zone us-central1-b --project grpc-testing

# This step subject to change, just easier to get one file to run a single test
wget https://raw.githubusercontent.com/grpc/test-infra/master/config/samples/go_example_loadtest.yaml

# The original version of the client is a bit old, update to the latest release
# version v1.21.0 .
kubectl version --client
curl -LO https://dl.k8s.io/release/v1.21.0/bin/linux/amd64/kubectl
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl
chmod +x kubectl
sudo mv kubectl $(which kubectl)
kubectl version --client

kubectl apply -f go_example_loadtest.yaml


echo "TODO: Add gRPC OSS Benchmarks here..."

