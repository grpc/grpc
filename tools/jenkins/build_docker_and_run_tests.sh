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

cd `dirname $0`/../..
git_root=`pwd`
cd -

# Ensure existence of ccache directory
mkdir -p /tmp/ccache

# Ensure existence of the home directory for XDG caches (e.g. what pip uses for
# its cache location now that --download-cache is deprecated).
mkdir -p /tmp/xdg-cache-home

# Create a local branch so the child Docker script won't complain
git branch -f jenkins-docker

# Use image name based on Dockerfile checksum
DOCKER_IMAGE_NAME=grpc_jenkins_slave${docker_suffix}_`sha1sum tools/jenkins/grpc_jenkins_slave/Dockerfile | cut -f1 -d\ `

# Make sure docker image has been built. Should be instantaneous if so.
docker build -t $DOCKER_IMAGE_NAME tools/jenkins/grpc_jenkins_slave$docker_suffix

# Choose random name for docker container
CONTAINER_NAME="run_tests_$(uuidgen)"

# Run tests inside docker
docker run \
  -e "RUN_TESTS_COMMAND=$RUN_TESTS_COMMAND" \
  -e "config=$config" \
  -e "arch=$arch" \
  -e CCACHE_DIR=/tmp/ccache \
  -e XDG_CACHE_HOME=/tmp/xdg-cache-home \
  -e THIS_IS_REALLY_NEEDED='                           ' \
  -i $TTY_FLAG \
  -v "$git_root:/var/local/jenkins/grpc" \
  -v /tmp/ccache:/tmp/ccache \
  -v /tmp/npm-cache:/tmp/npm-cache \
  -v /tmp/xdg-cache-home:/tmp/xdg-cache-home \
  -v /var/run/docker.sock:/var/run/docker.sock \
  -v $(which docker):/bin/docker \
  -w /var/local/git/grpc \
  --name=$CONTAINER_NAME \
  $DOCKER_IMAGE_NAME \
  bash -l /var/local/jenkins/grpc/tools/jenkins/docker_run_tests.sh || DOCKER_FAILED="true"

if [ "$XML_REPORT" != "" ]
then
  docker cp "$CONTAINER_NAME:/var/local/git/grpc/$XML_REPORT" $git_root
fi

docker cp "$CONTAINER_NAME:/var/local/git/grpc/reports.zip" $git_root || true
unzip $git_root/reports.zip -d $git_root || true
rm -f reports.zip

# remove the container, possibly killing it first
docker rm -f $CONTAINER_NAME || true

if [ "$DOCKER_FAILED" != "" ] && [ "$XML_REPORT" == "" ]
then
  exit 1
fi
