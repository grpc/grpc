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
#
# This script is invoked by run_interop_tests.py to build the docker image
# for interop testing. You should never need to call this script on your own.

set -x

# Params:
#  INTEROP_IMAGE - name of tag of the final interop image
#  BASE_NAME - base name used to locate the base Dockerfile and build script
#  TTY_FLAG - optional -t flag to make docker allocate tty
#  BUILD_INTEROP_DOCKER_EXTRA_ARGS - optional args to be passed to the
#    docker run command
#  GRPC_ROOT - grpc base directory, default to top of this tree.
#  GRPC_GO_ROOT - grpc-go base directory, default to '$GRPC_ROOT/../grpc-go'
#  GRPC_JAVA_ROOT - grpc-java base directory, default to '$GRPC_ROOT/../grpc-java'

cd `dirname $0`/../../..
echo "GRPC_ROOT: ${GRPC_ROOT:=$(pwd)}"
MOUNT_ARGS="-v $GRPC_ROOT:/var/local/jenkins/grpc:ro"

echo "GRPC_JAVA_ROOT: ${GRPC_JAVA_ROOT:=$(cd ../grpc-java && pwd)}"
if [ -n "$GRPC_JAVA_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_JAVA_ROOT:/var/local/jenkins/grpc-java:ro"
else
  echo "WARNING: grpc-java not found, it won't be mounted to the docker container."
fi

echo "GRPC_GO_ROOT: ${GRPC_GO_ROOT:=$(cd ../grpc-go && pwd)}"
if [ -n "$GRPC_GO_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_GO_ROOT:/var/local/jenkins/grpc-go:ro"
else
  echo "WARNING: grpc-go not found, it won't be mounted to the docker container."
fi

echo "GRPC_NODE_ROOT: ${GRPC_NODE_ROOT:=$(cd ../grpc-node && pwd)}"
if [ -n "$GRPC_NODE_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_NODE_ROOT:/var/local/jenkins/grpc-node:ro"
else
  echo "WARNING: grpc-node not found, it won't be mounted to the docker container."
fi

mkdir -p /tmp/ccache

# Mount service account dir if available.
# If service_directory does not contain the service account JSON file,
# some of the tests will fail.
if [ -e $HOME/service_account ]
then
  MOUNT_ARGS+=" -v $HOME/service_account:/var/local/jenkins/service_account:ro"
fi

# Use image name based on Dockerfile checksum
# on OSX use md5 instead of sha1sum
if which sha1sum > /dev/null;
then
  BASE_IMAGE=${BASE_NAME}_`sha1sum tools/dockerfile/interoptest/$BASE_NAME/Dockerfile | cut -f1 -d\ `
else
  BASE_IMAGE=${BASE_NAME}_`md5 -r tools/dockerfile/interoptest/$BASE_NAME/Dockerfile | cut -f1 -d\ `
fi

if [ "$DOCKERHUB_ORGANIZATION" != "" ]
then
  BASE_IMAGE=$DOCKERHUB_ORGANIZATION/$BASE_IMAGE
  time docker pull $BASE_IMAGE
else
  # Make sure docker image has been built. Should be instantaneous if so.
  docker build -t $BASE_IMAGE --force-rm=true tools/dockerfile/interoptest/$BASE_NAME || exit $?
fi

# Create a local branch so the child Docker script won't complain
git branch -f jenkins-docker

CONTAINER_NAME="build_${BASE_NAME}_$(uuidgen)"

# Prepare image for interop tests, commit it on success.
(docker run \
  -e CCACHE_DIR=/tmp/ccache \
  -e THIS_IS_REALLY_NEEDED='see https://github.com/docker/docker/issues/14203 for why docker is awful' \
  -e THIS_IS_REALLY_NEEDED_ONCE_AGAIN='For issue 4835. See https://github.com/docker/docker/issues/14203 for why docker is awful' \
  -i $TTY_FLAG \
  $MOUNT_ARGS \
  $BUILD_INTEROP_DOCKER_EXTRA_ARGS \
  -v /tmp/ccache:/tmp/ccache \
  --name=$CONTAINER_NAME \
  $BASE_IMAGE \
  bash -l /var/local/jenkins/grpc/tools/dockerfile/interoptest/$BASE_NAME/build_interop.sh \
  && docker commit $CONTAINER_NAME $INTEROP_IMAGE \
  && echo "Successfully built image $INTEROP_IMAGE")
EXITCODE=$?

# remove intermediate container, possibly killing it first
docker rm -f $CONTAINER_NAME

exit $EXITCODE
