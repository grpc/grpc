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

readonly IMAGE_NAME="gcr.io/grpc-testing/xds-interop/${LANGUAGE}-${APPLICATION}"

# Global variables
GIT_REPO="${GIT_REPO}"
GIT_COMMIT=""

pick_git_repo() {
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
}

fetch_latest_commit_sha() {
    GIT_COMMIT="$(git ls-remote "https://github.com/${GIT_REPO}" "${VERSION}" | grep "${VERSION}" | awk '{print $1}')"
}

build_image_if_needed() {
    list_tags="$(gcloud container images list-tags --filter="tags=${GIT_COMMIT}" "${IMAGE_NAME}")"
    if [[ -z "${list_tags}" ]]; then
        # Setup the build directory
        local build_dir
        build_dir="$(mktemp -d)"
        (cd "${build_dir}" && git clone --branch "${VERSION}" --depth 1 "https://github.com/${GIT_REPO}" . && git submodule update --init)
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
                curl "https://raw.githubusercontent.com/grpc/grpc-go/master/interop/xds/${APPLICATION}/Dockerfile" -O "${build_dir}/Dockerfile.${APPLICATION}"
                ;;
            java|php|ruby)
                # Java: the existing version compiles outside of Docker, not fully utilizing Cloud Build;
                # Ruby, PHP: there isn't an existing Dockerfile for xDS interop client/server
                cp "tools/run_tests/dockerize/xds/${LANGUAGE}/Dockerfile.${APPLICATION}" "${build_dir}/Dockerfile.${APPLICATION}"
                ;;
        esac
        # Trigger the Cloud Build job
        gcloud builds submit "${build_dir}" \
            --config "tools/run_tests/dockerize/xds/cloudbuild.yaml" \
            --substitutions "_IMAGE_NAME=${IMAGE_NAME},_GIT_COMMIT=${GIT_COMMIT},_APPLICATION=${APPLICATION},_VERSION=${VERSION}"
    else
        echo "Skip image build for ${LANGUAGE}-${APPLICATION}:${VERSION}"
    fi
}

main() {
    pick_git_repo
    fetch_latest_commit_sha
    build_image_if_needed
}

main

# ---------------------------------------------------------------------------------------------------------------------------------------
# ID                                    CREATE_TIME                DURATION  SOURCE                                                                                      IMAGES                                                 STATUS
# ecb4b321-54b6-4353-8201-d4c12c8854a2  2021-10-08T19:06:30+00:00  6M24S     gs://grpc-testing_cloudbuild/source/1633719989.214837-5bb68691f1014e9e8300d74e9c20f83f.tgz  gcr.io/grpc-testing/xds-interop/python-client:v1.33.x  SUCCESS

# ---------------------------------------------------------------------------------------------------------------------------------------
# ID                                    CREATE_TIME                DURATION  SOURCE                                                                                      IMAGES                                                           STATUS
# 0e096108-7e5e-4202-a9a9-063176d6972f  2021-10-08T19:23:22+00:00  6M42S     gs://grpc-testing_cloudbuild/source/1633721001.668342-de462a63148a4a9aa2f99ff51461c16a.tgz  gcr.io/grpc-testing/xds-interop/python-server:v1.33.x (+1 more)  SUCCESS

# Bazel generates runfiles environment with symlinks, which makes no
# sense to Cloud Build machines. We need to copy the Dockerfiles to a
# build directory, so the Dockerfiles can be uploaded properly.
# cp -r "tools/run_tests/dockerize/xds/${LANGUAGE}/." "${build_dir}"
