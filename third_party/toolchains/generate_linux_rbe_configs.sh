#!/bin/bash
# Copyright 2023 The gRPC Authors
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

# Generate linux RBE configs using rbe_configs_gen.
# See https://github.com/bazelbuild/bazel-toolchains#rbe_configs_gen---cli-tool-to-generate-configs

set -ex

cd $(dirname $0)/../..

REPO_ROOT="$(pwd)"

TEMP_DIR="$(mktemp -d)"
pushd "${TEMP_DIR}"
# Build the "rbe_configs_gen" binary using the official instructions.
git clone https://github.com/bazelbuild/bazel-toolchains.git
cd bazel-toolchains
# Build "rbe_configs_gen" binary under docker and put it to the current directory.
docker run --rm -v $PWD:/srcdir -w /srcdir golang:1.16 go build -o rbe_configs_gen ./cmd/rbe_configs_gen/rbe_configs_gen.go
popd

# location of the "rbe_config_gen" binary as build by the previous step.
RBE_CONFIGS_GEN_TOOL_PATH="${TEMP_DIR}/bazel-toolchains/rbe_configs_gen"

# Actions on RBE will run under a dedicated docker image from our collection of testing docker images.
LINUX_RBE_DOCKERFILE_DIR=tools/dockerfile/test/rbe_ubuntu2004
# Use the "current version" of the above dockerfile.
LINUX_RBE_DOCKER_IMAGE=$(cat ${LINUX_RBE_DOCKERFILE_DIR}.current_version)

# RBE currently has problems pulling images for Google Artifact Registry ("us-docker.pkg.dev/grpc-testing")
# so to workaround this, the original image was manually pushed to Google Container Registry ("gcr.io/grpc-testing")
# as well and RBE will use the mirrored image instead. See b/275571385
# TODO(jtattermusch): get rid of this hack.
LINUX_RBE_DOCKER_IMAGE_IN_GCR=$(echo -n "${LINUX_RBE_DOCKER_IMAGE}" | sed 's|^us-docker.pkg.dev/grpc-testing/testing-images-public/|gcr.io/grpc-testing/rbe_images_mirror/|')

# Bazel version used for configuring
# Needs to be one of the versions from bazel/supported_versions.txt chosen so that the result is compatible
# with other supported bazel versions.
BAZEL_VERSION=5.4.0

# TODO(jtattermusch): experiment with --cpp_env_json to simplify bazel build configuration.

# Where to store the generated configs (relative to repo root)
CONFIG_OUTPUT_PATH=third_party/toolchains/rbe_ubuntu2004

# Delete old generated configs.
rm -rf "${REPO_ROOT}/${CONFIG_OUTPUT_PATH}"

${RBE_CONFIGS_GEN_TOOL_PATH} \
    --bazel_version="${BAZEL_VERSION}" \
    --toolchain_container="${LINUX_RBE_DOCKER_IMAGE_IN_GCR}" \
    --output_src_root="${REPO_ROOT}" \
    --output_config_path="${CONFIG_OUTPUT_PATH}" \
    --exec_os=linux \
    --target_os=linux \
    --generate_java_configs=false
