#!/bin/bash
# Copyright 2015 gRPC authors.
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

# Creates a standard jenkins worker on GCE.

set -ex

cd $(dirname $0)

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-a

INSTANCE_NAME="${1:-grpc-jenkins-worker1}"

gcloud compute instances create $INSTANCE_NAME \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type n1-standard-16 \
    --image=ubuntu-1510 \
    --image-project=grpc-testing \
    --boot-disk-size 1000 \
    --scopes https://www.googleapis.com/auth/bigquery \
    --tags=allow-ssh

echo 'Created GCE instance, waiting 60 seconds for it to come online.'
sleep 60

gcloud compute copy-files \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    jenkins_master.pub linux_worker_init.sh ${INSTANCE_NAME}:~

gcloud compute ssh \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    $INSTANCE_NAME --command "./linux_worker_init.sh"
