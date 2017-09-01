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

# Creates a performance worker on GCE.
# IMPORTANT: After creating the worker, one needs to manually add the pubkey
# of jenkins@the-machine-where-jenkins-starts-perf-tests
# to ~/.ssh/authorized_keys so that multi-machine scenarios can work.
# See tools/run_tests/run_performance_tests.py for details.

set -ex

cd $(dirname $0)

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-b  # this zone allows 32core machines

INSTANCE_NAME="${1:-grpc-performance-server1}"
MACHINE_TYPE=n1-standard-32

gcloud compute instances create $INSTANCE_NAME \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type $MACHINE_TYPE \
    --image-project ubuntu-os-cloud \
    --image-family ubuntu-1610 \
    --boot-disk-size 300 \
    --scopes https://www.googleapis.com/auth/bigquery

echo 'Created GCE instance, waiting 60 seconds for it to come online.'
sleep 60

gcloud compute copy-files \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    jenkins_master.pub linux_performance_worker_init.sh jenkins@${INSTANCE_NAME}:~

gcloud compute ssh \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    jenkins@${INSTANCE_NAME} --command "./linux_performance_worker_init.sh"
