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
# Builds selected testing docker images and pushes them to artifact registry.
# NOTE: These images are not intended to be used by gRPC end users,
# they simply provide an easily reproducible environment for running gRPC
# tests.

set -e

cd $(dirname $0)/../..
git_root=$(pwd)
cd -

# How to configure docker before running this script for the first time:
# Configure docker:
# $ gcloud auth configure-docker us-docker.pkg.dev
# Login with gcloud:
# $ gcloud auth login

# Check that docker is installed and sudoless docker works.
docker run --rm -it debian:11 bash -c 'echo "sudoless docker run works!"' || \
    (echo "Error: docker not installed or sudoless docker doesn't work?" && exit 1)

# Some of the images we build are for arm64 architecture and the easiest
# way of allowing them to build locally on x64 machine is to use
# qemu binfmt-misc hook that automatically runs arm64 binaries under
# an emulator.
# Perform a check that "qemu-user-static" with binfmt-misc hook
# is installed, to give an early warning (otherwise building arm64 images won't work)
docker run --rm -it arm64v8/debian:11 bash -c 'echo "able to run arm64 docker images with an emulator!"' || \
    (echo "Error: can't run arm64 images under an emulator. Have you run 'sudo apt-get install qemu-user-static'?" && exit 1)

ARTIFACT_REGISTRY_PREFIX=us-docker.pkg.dev/grpc-testing/testing-images-public

# all dockerfile definitions we use for testing and for which we push an image to the registry
ALL_DOCKERFILE_DIRS=(
  tools/dockerfile/test/*
  tools/dockerfile/grpc_artifact_*
  tools/dockerfile/interoptest/*
  tools/dockerfile/distribtest/*
  third_party/rake-compiler-dock/*/
)

for DOCKERFILE_DIR in "${ALL_DOCKERFILE_DIRS[@]}"
do
  # Generate image name based on Dockerfile checksum. That works well as long
  # as can count on dockerfiles being written in a way that changing the logical 
  # contents of the docker image always changes the SHA (e.g. using "ADD file" 
  # cmd in the dockerfile in not ok as contents of the added file will not be
  # reflected in the SHA).
  DOCKER_IMAGE_NAME=$(basename $DOCKERFILE_DIR)

  if [ ! -e "$DOCKERFILE_DIR/Dockerfile" ]; then
    continue
  else
    DOCKER_IMAGE_TAG=$(sha1sum $DOCKERFILE_DIR/Dockerfile | cut -f1 -d\ )
  fi

  echo "Visiting ${DOCKERFILE_DIR}"

  # SKIP_REMOTE controls whether artifact registry are going to be queried at all.
  if [ "${SKIP_REMOTE}" == "" ]
  then
    DOCKER_IMAGE_DIGEST_REMOTE=$(gcloud artifacts docker images describe "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" --format=json | jq -r '.image_summary.digest')

    if [ "${DOCKER_IMAGE_DIGEST_REMOTE}" != "" ]
    then
      # skip building the image if it already exists in the destination registry
      echo "Docker image ${DOCKER_IMAGE_NAME} already exists in artifact registry at the right version (tag ${DOCKER_IMAGE_TAG})."

      # TODO: (sanity check) if remote check requested, check that the remote digest matches what's currently in the .current_version file.

      # update info on what we consider to be the current version of the docker image (which will be used to run tests)
      # we consider the sha256 image digest info from the artifact registry to be the canonical one
      echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_REMOTE}" >${DOCKERFILE_DIR}.current_version

      continue
    fi

    # TODO: (sanity check) if remote check requested, fail here since not all images have been uploaded to artifact registry
  fi

  # if the .current_version file doesn't exist or it doesn't contain the right SHA checksum,
  # it is out of date and we will need to rebuild the docker image locally.
  LOCAL_BUILD_REQUIRED=""
  grep "^${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@sha256:.*" ${DOCKERFILE_DIR}.current_version >/dev/null && LOCAL_BUILD_REQUIRED=true

  if [ "${LOCAL_BUILD_REQUIRED}" != "" ]
  then
    echo "Dockerfile for ${DOCKER_IMAGE_NAME} hasn't changed. Will skip 'docker build'."
    continue
  fi

  # TODO: (sanity check) if in check mode, fail here since the .current_version file is either missing or out of date

  if [ "${TRANSFER_FROM_DOCKERHUB}" == "" ]
  then
    echo "Running 'docker build' for ${DOCKER_IMAGE_NAME}"
    echo "=========="
    docker build -t ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG} ${DOCKERFILE_DIR}
    echo "=========="
  else
    # TRANSFER_FROM_DOCKERHUB is a temporary feature that pulls the corresponding image from dockerhub instead
    # of building it from scratch locally. This should simplify the dockerhub -> artifact registry migration.
    DOCKERHUB_ORGANIZATION=grpctesting
    # pull image from dockerhub
    docker pull ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
    # add the artifact registry tag
    docker tag ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG} ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
  fi

  DOCKER_IMAGE_DIGEST_LOCAL=$(docker image inspect "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" | jq -e -r '.[0].Id')

  # update info on what we consider to be the current version of the docker image (which will be used to run tests)
  echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_LOCAL}" >${DOCKERFILE_DIR}.current_version

  if [ "${SKIP_UPLOAD}" == "" ]
  then
    docker push ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
  fi
done

# TODO: extra sanity checks - check there are no extra current_version files (for which there isn't a corresponding Dockerfile)

echo "All done."
