#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Creates a standard jenkins worker on GCE.

set -ex

cd $(dirname $0)

CLOUD_PROJECT=grpc-testing
ZONE=us-central1-a

INSTANCE_NAME="${1:-grpc-jenkins-worker1}"

gcloud compute instances create $INSTANCE_NAME \
    --project="$CLOUD_PROJECT" \
    --zone "$ZONE" \
    --machine-type n1-standard-8 \
    --image ubuntu-15-10 \
    --boot-disk-size 1000

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
