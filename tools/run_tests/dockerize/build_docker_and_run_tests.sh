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
# This script is invoked by run_tests.py to accommodate "test under docker"
# scenario. You should never need to call this script on your own.

set -ex

cd $(dirname $0)/../../..
git_root=$(pwd)
cd -

# Ensure existence of ccache directory
mkdir -p /tmp/ccache

# Ensure existence of the home directory for XDG caches (e.g. what pip uses for
# its cache location now that --download-cache is deprecated).
mkdir -p /tmp/xdg-cache-home

# Inputs
# DOCKERFILE_DIR - Directory in which Dockerfile file is located.
# DOCKER_RUN_SCRIPT - Script to run under docker (relative to grpc repo root)
# DOCKERHUB_ORGANIZATION - If set, pull a prebuilt image from given dockerhub org.

# Use image name based on Dockerfile location checksum
DOCKER_IMAGE_NAME=$(basename $DOCKERFILE_DIR)_$(sha1sum $DOCKERFILE_DIR/Dockerfile | cut -f1 -d\ )

if [ "$DOCKERHUB_ORGANIZATION" != "" ]
then
  DOCKER_IMAGE_NAME=$DOCKERHUB_ORGANIZATION/$DOCKER_IMAGE_NAME
  docker pull $DOCKER_IMAGE_NAME
else
  # Make sure docker image has been built. Should be instantaneous if so.
  docker build -t $DOCKER_IMAGE_NAME $DOCKERFILE_DIR
fi

# Choose random name for docker container
CONTAINER_NAME="run_tests_$(uuidgen)"

# Git root as seen by the docker instance
docker_instance_git_root=/var/local/jenkins/grpc

# Run tests inside docker
DOCKER_EXIT_CODE=0
docker run \
  -e "RUN_TESTS_COMMAND=$RUN_TESTS_COMMAND" \
  -e "config=$config" \
  -e "arch=$arch" \
  -e CCACHE_DIR=/tmp/ccache \
  -e XDG_CACHE_HOME=/tmp/xdg-cache-home \
  -e THIS_IS_REALLY_NEEDED='see https://github.com/docker/docker/issues/14203 for why docker is awful' \
  -e HOST_GIT_ROOT=$git_root \
  -e LOCAL_GIT_ROOT=$docker_instance_git_root \
  -e "BUILD_ID=$BUILD_ID" \
  -i $TTY_FLAG \
  -v "$git_root:$docker_instance_git_root" \
  -v /tmp/ccache:/tmp/ccache \
  -v /tmp/npm-cache:/tmp/npm-cache \
  -v /tmp/xdg-cache-home:/tmp/xdg-cache-home \
  -w /var/local/git/grpc \
  --name=$CONTAINER_NAME \
  $DOCKER_IMAGE_NAME \
  bash -l "/var/local/jenkins/grpc/$DOCKER_RUN_SCRIPT" || DOCKER_EXIT_CODE=$?

# use unique name for reports.zip to prevent clash between concurrent
# run_tests.py runs 
TEMP_REPORTS_ZIP=`mktemp`
docker cp "$CONTAINER_NAME:/var/local/git/grpc/reports.zip" ${TEMP_REPORTS_ZIP} || true
unzip -o ${TEMP_REPORTS_ZIP} -d $git_root || true
rm -f ${TEMP_REPORTS_ZIP}

# remove the container, possibly killing it first
docker rm -f $CONTAINER_NAME || true

exit $DOCKER_EXIT_CODE
