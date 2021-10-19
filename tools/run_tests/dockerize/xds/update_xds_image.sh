#! /bin/bash
# Copyright 2021 The gRPC Authors
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

set -ex

# Input arguments
APPLICATION="$1"
LANGUAGE="$2"
VERSION="$3"
FORCE_IMAGE_BUILD="${FORCE_IMAGE_BUILD-}"

# Constants
IMAGE_NAME="gcr.io/grpc-testing/xds-interop/${LANGUAGE}-${APPLICATION}"

# Decide the upstream gRPC repo
GIT_REPO="${GIT_REPO}"
if [[ -z "${GIT_REPO}" ]]; then
    case "${LANGUAGE}" in
        python|cpp|php|ruby)
            GIT_REPO="grpc/grpc"
            ;;
        java)
            GIT_REPO="grpc/grpc-java"
            ;;
        go)
            GIT_REPO="grpc/grpc-go"
            ;;
        *)
            echo "Unknown language ${LANGUAGE}"
            exit 1
            ;;
    esac
fi

# If no build commit is specified, fetch latest Git commit for the given gRPC version
if [[ -z "${BUILD_COMMIT}" ]]; then
    BUILD_COMMIT="$(git ls-remote "https://github.com/${GIT_REPO}" "${VERSION}" | grep "${VERSION}" | awk '{print $1}')"
fi

# Create a buffer layer for GCloud command to write stuff.
# NOTE(lidiz) GCloud needs to write log and credentials. The former is
# configurable, but the latter is not. Searched the GCloud config list and its
# source code, I haven't found a concrete reason why writing the credentials is
# necessary, since we are only to read the creds to authenticate.
# https://cloud.google.com/sdk/gcloud/reference/config/set
# NOTE(lidiz) The GCloud writing demand is an issue for Bazel's sandboxing. "The
# current user must have a home directory... Tests must not attempt to write to
# it." The credentials sits at ~/.config/gcloud, which is directly challenging
# Bazel's policy. Hence, we need this extra layer to make peace between GCloud
# and Bazel.
# https://docs.bazel.build/versions/main/test-encyclopedia.html#users-and-groups
gcloud_dir="$(mktemp -d)"
mkdir -p "${gcloud_dir}/.config/gcloud"
cp -r "${HOME}/.config/gcloud/." "${gcloud_dir}/.config/gcloud"

# Check if the image is already existed
list_tags="$(HOME="${gcloud_dir}" gcloud container images list-tags --filter="tags=${BUILD_COMMIT}" "${IMAGE_NAME}")"
if [[ -n "${FORCE_IMAGE_BUILD}" || -z "${list_tags}" ]]; then
    # Setup the build directory
    build_dir="$(mktemp -d)"
    # Shallow copy the gRPC repo and its submodules
    (cd "${build_dir}" && \
        git clone --branch "${VERSION}" --depth 1 "https://github.com/${GIT_REPO}" . && \
        git submodule update --init --depth=1)
    # Choosing the Dockerfile to build images
    case "${LANGUAGE}" in
        python)
            # Reuse existing Python Dockerfile
            cp "src/python/grpcio_tests/tests_py3_only/interop/Dockerfile.${APPLICATION}" "${build_dir}/Dockerfile.${APPLICATION}"
            ;;
        cpp)
            # Reuse existing C++ Dockerfile
            cp "tools/dockerfile/interoptest/grpc_interop_cxx_xds/Dockerfile.xds_${APPLICATION}" "${build_dir}/Dockerfile.${APPLICATION}"
            ;;
        go)
            # Reuse existing Go Dockerfile
            curl "https://raw.githubusercontent.com/${GIT_REPO}/master/interop/xds/${APPLICATION}/Dockerfile" > "${build_dir}/Dockerfile.${APPLICATION}"
            ;;
        java|php|ruby)
            # Java: the existing version compiles outside of Docker, not fully utilizing Cloud Build;
            # Ruby, PHP: there isn't an existing Dockerfile for xDS interop client/server
            cp "tools/run_tests/dockerize/xds/${LANGUAGE}/Dockerfile.${APPLICATION}" "${build_dir}/Dockerfile.${APPLICATION}"
            ;;
        *)
            echo "Unknown language ${LANGUAGE}"
            exit 1
            ;;
    esac
    # Trigger the Cloud Build job
    HOME="${gcloud_dir}" gcloud builds submit "${build_dir}" \
        --config "tools/run_tests/dockerize/xds/cloudbuild.yaml" \
        --substitutions "_IMAGE_NAME=${IMAGE_NAME},_BUILD_COMMIT=${BUILD_COMMIT},_APPLICATION=${APPLICATION},_VERSION=${VERSION}"
else
    echo "Skip image build for ${LANGUAGE}-${APPLICATION}:${VERSION}"
fi
