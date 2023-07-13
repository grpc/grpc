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

# Recognized env variables that can be used as params.
#  LOCAL_ONLY_MODE: if set (e.g. LOCAL_ONLY_MODE=true), script will only operate locally and it won't query artifact registry and won't upload to it.
#  CHECK_MODE: if set, the script will check that all the .current_version files are up-to-date (used by sanity tests).
#  SKIP_UPLOAD: if set, script won't push docker images it built to artifact registry.
#  TRANSFER_FROM_DOCKERHUB: if set, will attempt to grab docker images missing in artifact registry from dockerhub instead of building them from scratch locally.

# How to configure docker before running this script for the first time:
# Configure docker:
# $ gcloud auth configure-docker us-docker.pkg.dev
# Login with gcloud:
# $ gcloud auth login

# Various check that the environment is setup correctly.
# The enviroment checks are skipped when running as a sanity check on CI.
if [ "${CHECK_MODE}" == "" ]
then
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
fi

ARTIFACT_REGISTRY_PREFIX=us-docker.pkg.dev/grpc-testing/testing-images-public

# all dockerfile definitions we use for testing and for which we push an image to the registry
ALL_DOCKERFILE_DIRS=(
  tools/dockerfile/test/*
  tools/dockerfile/grpc_artifact_*
  tools/dockerfile/interoptest/*
  tools/dockerfile/distribtest/*
  third_party/rake-compiler-dock/*
)

CHECK_FAILED=""

if [ "${CHECK_MODE}" != "" ]
then
  # Check that there are no stale .current_version files (for which the corresponding
  # dockerfile_dir doesn't exist anymore).
  for CURRENTVERSION_FILE in $(find tools/ third_party/rake-compiler-dock -name '*.current_version')
  do
    DOCKERFILE_DIR="$(echo ${CURRENTVERSION_FILE} | sed 's/.current_version$//')"
    if [ ! -e "${DOCKERFILE_DIR}/Dockerfile" ]
    then
       echo "Found that ${DOCKERFILE_DIR} has '.current_version' file but there is no corresponding Dockerfile."
       echo "Should the ${CURRENTVERSION_FILE} file be deleted?"
       CHECK_FAILED=true
    fi
  done
fi

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

  if [ "${LOCAL_ONLY_MODE}" == "" ]
  then
    DOCKER_IMAGE_DIGEST_REMOTE=$(gcloud artifacts docker images describe "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" --format=json | jq -r '.image_summary.digest')

    if [ "${DOCKER_IMAGE_DIGEST_REMOTE}" != "" ]
    then
      # skip building the image if it already exists in the destination registry
      echo "Docker image ${DOCKER_IMAGE_NAME} already exists in artifact registry at the right version (tag ${DOCKER_IMAGE_TAG})."

      VERSION_FILE_OUT_OF_DATE=""
      grep "^${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_REMOTE}$" ${DOCKERFILE_DIR}.current_version >/dev/null || VERSION_FILE_OUT_OF_DATE="true"

      if [ "${VERSION_FILE_OUT_OF_DATE}" == "" ]
      then
        echo "Version file for ${DOCKER_IMAGE_NAME} is in sync with info from artifact registry."
        continue
      fi

      if [ "${CHECK_MODE}" != "" ]
      then
        echo "CHECK FAILED: Version file ${DOCKERFILE_DIR}.current_version is not in sync with info from artifact registry."
        CHECK_FAILED=true
        continue
      fi

      # update info on what we consider to be the current version of the docker image (which will be used to run tests)
      # we consider the sha256 image digest info from the artifact registry to be the canonical one
      echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_REMOTE}" >${DOCKERFILE_DIR}.current_version

      continue
    fi

    if [ "${CHECK_MODE}" != "" ]
    then
      echo "CHECK FAILED: Docker image ${DOCKER_IMAGE_NAME} not found in artifact registry."
      CHECK_FAILED=true
      continue
    fi

  else
    echo "Skipped querying artifact registry (running in local-only mode)."
  fi

  # if the .current_version file doesn't exist or it doesn't contain the right SHA checksum,
  # it is out of date and we will need to rebuild the docker image locally.
  LOCAL_BUILD_REQUIRED=""
  grep "^${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@sha256:.*" ${DOCKERFILE_DIR}.current_version >/dev/null || LOCAL_BUILD_REQUIRED=true

  if [ "${LOCAL_BUILD_REQUIRED}" == "" ]
  then
    echo "Dockerfile for ${DOCKER_IMAGE_NAME} hasn't changed. Will skip 'docker build'."
    continue
  fi

  if [ "${CHECK_MODE}" != "" ]
  then
    echo "CHECK FAILED: Dockerfile for ${DOCKER_IMAGE_NAME} has changed, but the ${DOCKERFILE_DIR}.current_version is not up to date."
    CHECK_FAILED=true
    continue
  fi

  if [ "${TRANSFER_FROM_DOCKERHUB}" == "" ]
  then
    echo "Running 'docker build' for ${DOCKER_IMAGE_NAME}"
    echo "=========="
    docker build -t ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG} ${DOCKERFILE_DIR}
    echo "=========="
  else
    # TRANSFER_FROM_DOCKERHUB is a temporary feature that pulls the corresponding image from dockerhub instead
    # of building it from scratch locally. This should simplify the dockerhub -> artifact registry migration.
    # TODO(jtattermusch): remove this feature in Q1 2023.
    DOCKERHUB_ORGANIZATION=grpctesting
    # pull image from dockerhub
    docker pull ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
    # add the artifact registry tag
    docker tag ${DOCKERHUB_ORGANIZATION}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG} ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
  fi

  DOCKER_IMAGE_DIGEST_LOCAL=$(docker image inspect "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" | jq -e -r '.[0].Id')

  # update info on what we consider to be the current version of the docker image (which will be used to run tests)
  # TODO(jtattermusch): If we just built the docker image locally,
  # the local image digest will be different than the digest as reported by container registry.
  # See b/278226801
  echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_LOCAL}" >${DOCKERFILE_DIR}.current_version

  if [ "${SKIP_UPLOAD}" == "" ] && [ "${LOCAL_ONLY_MODE}" == "" ]
  then
    docker push ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}
  fi
done

if [ "${CHECK_FAILED}" != "" ]
then
  echo "ERROR: Some checks have failed."
  exit 1
fi

echo "All done."
