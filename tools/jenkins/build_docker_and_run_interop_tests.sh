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
#
# This script is invoked by run_interop_tests.py to accommodate
# "interop tests under docker" scenario. You should never need to call this
# script on your own.

set -ex

cd `dirname $0`/../..
git_root=`pwd`
cd -

mkdir -p /tmp/ccache

# Use image name based on Dockerfile checksum
DOCKER_IMAGE_NAME=grpc_jenkins_slave${docker_suffix}_`sha1sum tools/jenkins/grpc_jenkins_slave/Dockerfile | cut -f1 -d\ `

# Make sure docker image has been built. Should be instantaneous if so.
docker build -t $DOCKER_IMAGE_NAME tools/jenkins/grpc_jenkins_slave$docker_suffix

# Create a local branch so the child Docker script won't complain
git branch -f jenkins-docker

# Make sure the CID files are gone.
rm -f prepare.cid server.cid client.cid

# Prepare image for interop tests
docker run \
  -e CCACHE_DIR=/tmp/ccache \
  -i $TTY_FLAG \
  -v "$git_root:/var/local/jenkins/grpc" \
  -v /tmp/ccache:/tmp/ccache \
  --cidfile=prepare.cid \
  $DOCKER_IMAGE_NAME \
  bash -l /var/local/jenkins/grpc/tools/jenkins/docker_prepare_interop_tests.sh || DOCKER_FAILED="true"

PREPARE_CID=`cat prepare.cid`

# Create image from the container, we will spawn one docker for clients
# and one for servers.
INTEROP_IMAGE=interop_`uuidgen`
docker commit $PREPARE_CID $INTEROP_IMAGE
# remove container, possibly killing it first
docker rm -f $PREPARE_CID || true
echo "Successfully built image $INTEROP_IMAGE"

# run interop servers under docker in the background
docker run \
  -d -i \
  $SERVERS_DOCKER_EXTRA_ARGS \
  --cidfile=server.cid \
  $INTEROP_IMAGE bash -l /var/local/git/grpc/tools/jenkins/docker_run_interop_servers.sh

SERVER_CID=`cat server.cid`

SERVER_PORTS=""
for tuple in $SERVER_PORT_TUPLES
do
  # lookup under which port docker exposes given internal port
  exposed_port=`docker port $SERVER_CID ${tuple#*:} | awk -F ":" '{print $NF}'`

  # override the port for corresponding cloud_to_cloud server
  SERVER_PORTS+=" --override_server ${tuple%:*}=localhost:$exposed_port"
  echo "${tuple%:*} server is exposed under port $exposed_port"
done

# run interop clients
docker run \
  -e "RUN_TESTS_COMMAND=$RUN_TESTS_COMMAND $SERVER_PORTS" \
  -w /var/local/git/grpc \
  -i $TTY_FLAG \
  --net=host \
  --cidfile=client.cid \
  $INTEROP_IMAGE bash -l /var/local/git/grpc/tools/jenkins/docker_run_interop_tests.sh || DOCKER_FAILED="true"

CLIENT_CID=`cat client.cid`

echo "killing and removing server container $SERVER_CID"
docker rm -f $SERVER_CID || true

docker cp $CLIENT_CID:/var/local/git/grpc/report.xml $git_root
docker rm -f $CLIENT_CID || true
docker rmi -f $DOCKER_IMAGE_NAME || true
