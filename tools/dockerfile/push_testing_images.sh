#!/bin/bash
# Copyright 2016 gRPC authors.
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

for DOCKERFILE_DIR in tools/dockerfile/test/* tools/dockerfile/grpc_artifact_* tools/dockerfile/interoptest/* tools/dockerfile/distribtest/* third_party/rake-compiler-dock/*/
do
  # Generate image name based on Dockerfile checksum. That works well as long
  # as can count on dockerfiles being written in a way that changing the logical 
  # contents of the docker image always changes the SHA (e.g. using "ADD file" 
  # cmd in the dockerfile in not ok as contents of the added file will not be
  # reflected in the SHA).
  DOCKER_IMAGE_NAME=$(basename $DOCKERFILE_DIR)
  DOCKER_IMAGE_TAG=$(sha1sum $DOCKERFILE_DIR/Dockerfile | cut -f1 -d\ )

  # skip the image if it already exists in the repo 
  curl --silent -f -lSL https://registry.hub.docker.com/v2/repositories/${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}/tags/${DOCKER_IMAGE_TAG} > /dev/null \
      && continue

  docker build -t ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG} ${DOCKERFILE_DIR}

  # "docker login" needs to be run in advance
  docker push ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
done
