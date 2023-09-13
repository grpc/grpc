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

# Process a directory.
# Arguments:
#   $1 - directory containing the dockerfile
function process_dir {
  DOCKERFILE_DIR=$1

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
    # value obtained here corresponds to the "RepoDigests" from "docker image inspect", but without the need to actually pull the image
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
  grep "^${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@sha256:.*$" ${DOCKERFILE_DIR}.current_version >/dev/null || LOCAL_BUILD_REQUIRED=true

  # If the current version file has contains SHA checksum, but not the remote image digest,
  # it means the locally-built image hasn't been pushed to artifact registry yet.
  DIGEST_MISSING_IN_CURRENT_VERSION_FILE=""
  grep "^${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}$" ${DOCKERFILE_DIR}.current_version >/dev/null && DIGEST_MISSING_IN_CURRENT_VERSION_FILE=true

  if [ "${LOCAL_BUILD_REQUIRED}" == "" ]
  then
    echo "Dockerfile for ${DOCKER_IMAGE_NAME} hasn't changed. Will skip 'docker build'."
    continue
  fi

  if [ "${CHECK_MODE}" != "" ] && [ "${DIGEST_MISSING_IN_CURRENT_VERSION_FILE}" != "" ]
  then
    echo "CHECK FAILED: Dockerfile for ${DOCKER_IMAGE_NAME} has changed and was built locally, but looks like it hasn't been pushed."
    CHECK_FAILED=true
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

  # After building the docker image locally, we don't know the image's RepoDigest (which is distinct from image's "Id" digest) yet
  # so we can only update the .current_version file with the image tag (which will be enough for running tests under docker locally).
  # The .current_version file will be updated with both tag and SHA256 repo digest later, once we actually push it.
  # See b/278226801 for context.
  echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" >${DOCKERFILE_DIR}.current_version

  if [ "${SKIP_UPLOAD}" == "" ] && [ "${LOCAL_ONLY_MODE}" == "" ]
  then
    docker push ${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}

    # After successful push, the image's RepoDigest info will become available in "docker image inspect",
    # so we update the .current_version file with the repo digest.
    DOCKER_IMAGE_DIGEST_REMOTE=$(docker image inspect "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}" | jq -e -r ".[0].RepoDigests[] | select(contains(\"${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}@\"))" | sed 's/^.*@sha256:/sha256:/')
    echo -n "${ARTIFACT_REGISTRY_PREFIX}/${DOCKER_IMAGE_NAME}:${DOCKER_IMAGE_TAG}@${DOCKER_IMAGE_DIGEST_REMOTE}" >${DOCKERFILE_DIR}.current_version
  fi
}


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
  docker run --rm -it -v /usr/bin/qemu-arm-static:/usr/bin/qemu-arm-static arm64v8/debian:11 bash -c 'echo "able to run arm64 docker images with an emulator!"' || \
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

if [ "$1" != "" ]
then
  process_dir $1
else
  for DOCKERFILE_DIR in "${ALL_DOCKERFILE_DIRS[@]}"
  do
    process_dir $DOCKERFILE_DIR
  done
fi

if [ "${CHECK_MODE}" != "" ]
then
    # Check that dockerimage_current_versions.bzl is up to date.
    CHECK_MODE="${CHECK_MODE}" tools/bazelify_tests/generate_dockerimage_current_versions_bzl.sh || CHECK_FAILED=true
else
    # Regenerate dockerimage_current_versions.bzl
    tools/bazelify_tests/generate_dockerimage_current_versions_bzl.sh
fi

if [ "${CHECK_FAILED}" != "" ]
then
  echo "ERROR: Some checks have failed."
  exit 1
fi

echo "All done."
