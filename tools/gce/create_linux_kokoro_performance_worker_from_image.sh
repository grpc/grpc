#!/bin/bash
# Copyright 2018 The gRPC Authors
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

# Creates a performance worker on GCE from an image that's used for kokoro
# perf workers.

set -ex

cd "$(dirname "$0")"

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-b  # this zone allows 32core machines
LATEST_PERF_WORKER_IMAGE=grpc-performance-kokoro-v5  # update if newer image exists

INSTANCE_NAME="${1:-grpc-kokoro-performance-server}"
MACHINE_TYPE="${2:-e2-standard-32}"

gcloud compute instances create "$INSTANCE_NAME" \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type "$MACHINE_TYPE" \
    --image-project "$CLOUD_PROJECT" \
    --image "$LATEST_PERF_WORKER_IMAGE" \
    --boot-disk-size 300 \
    --scopes https://www.googleapis.com/auth/bigquery \
    --tags=allow-ssh
