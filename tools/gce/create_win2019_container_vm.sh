#!/bin/bash
# Copyright 2017 gRPC authors.
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

# Creates a worker for debugging/experiments.
# The worker will have all the prerequisites that are installed on kokoro
# windows workers.

set -ex

cd "$(dirname "$0")"

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-b

if [ "$1" != "" ]
then
  INSTANCE_NAME="$1"
else
  INSTANCE_NAME="${USER}-win2019-for-containers-test1"
fi

MACHINE_TYPE=e2-standard-8

# The image version might need updating.
gcloud compute instances create "$INSTANCE_NAME" \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type "$MACHINE_TYPE" \
    --boot-disk-size=400GB \
    --boot-disk-type pd-ssd \
    --image-project=windows-cloud \
    --image-family=windows-2019-for-containers

# or use --image-family=windows-2019-core-for-containers
