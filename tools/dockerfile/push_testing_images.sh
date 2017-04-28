#!/bin/bash
# Copyright 2016, Google Inc.
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
# Builds selected testing docker images and pushes them to dockerhub.
# Useful for testing environments where it's impractical (or impossible)
# to rely on docker images being cached locally after they've been built
# for the first time (which might be costly especially for some images).
# NOTE: gRPC docker images intended to be used by end users are NOT
# pushed using this script (they're built automatically by dockerhub).
# This script is only for "internal" images we use when testing gRPC.  

set -ex

cd $(dirname $0)/../..
git_root=$(pwd)
cd -

DOCKERHUB_ORGANIZATION=grpctesting

for DOCKERFILE_DIR in tools/dockerfile/test/*
do
  # Generate image name based on Dockerfile checksum. That works well as long
  # as can count on dockerfiles being written in a way that changing the logical 
  # contents of the docker image always changes the SHA (e.g. using "ADD file" 
  # cmd in the dockerfile in not ok as contents of the added file will not be
  # reflected in the SHA).
  DOCKER_IMAGE_NAME=$(basename $DOCKERFILE_DIR)_$(sha1sum $DOCKERFILE_DIR/Dockerfile | cut -f1 -d\ )

  # skip the image if it already exists in the repo 
  curl --silent -f -lSL https://registry.hub.docker.com/v2/repositories/${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}/tags/latest > /dev/null \
      && continue

  docker build -t ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME} ${DOCKERFILE_DIR}
      
  # "docker login" needs to be run in advance
  docker push ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}
done
