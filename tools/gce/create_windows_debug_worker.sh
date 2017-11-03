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

cd $(dirname $0)

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-b

if [ "$1" != "" ]
then
  INSTANCE_NAME="$1"
else
  INSTANCE_NAME="${USER}-windows-kokoro-debug1"
fi

MACHINE_TYPE=n1-standard-8
TMP_DISK_NAME="$INSTANCE_NAME-temp-disk"

gcloud compute disks create $TMP_DISK_NAME \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --image-project google.com:kokoro \
    --image empty-100g-image \
    --type pd-ssd

echo 'Created scratch disk, waiting for it to become available.'
sleep 15

gcloud compute instances create $INSTANCE_NAME \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type $MACHINE_TYPE \
    --image-project google.com:kokoro \
    --image kokoro-win7build-v9-prod-debug \
    --boot-disk-size 500 \
    --boot-disk-type pd-ssd \
    --tags=allow-ssh \
    --disk auto-delete=yes,boot=no,name=$TMP_DISK_NAME
