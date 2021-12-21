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

cd "$(dirname "$0")/../../.."

pyenv local 3.6.1
python3 -m pip install dataclasses

gcloud container clusters get-credentials interop-test-psm-sec-v2-us-central1-a --zone us-central1-a --project grpc-testing
# flag resource_prefix is required by the gke test framework, but doesn't
# matter for the cleanup script.
python3 tools/run_tests/xds_k8s_test_driver/bin/cleanup/cleanup.py\
    --project=grpc-testing\
    --network=default-vpc\
    --kube_context=gke_grpc-testing_us-central1-a_interop-test-psm-sec-v2-us-central1-a\
    --resource_prefix='required but does not matter'\
    --td_bootstrap_image='required but does not matter' --server_image='required but does not matter' --client_image='required but does not matter'