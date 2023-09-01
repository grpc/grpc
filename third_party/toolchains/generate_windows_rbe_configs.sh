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

# Generate windows RBE configs using rbe_configs_gen.
# See https://github.com/bazelbuild/bazel-toolchains#rbe_configs_gen---cli-tool-to-generate-configs

set -ex

cd $(dirname $0)/../..

REPO_ROOT="$(pwd)"

# Download a recent version of rbe_configs_gen binary
wget https://github.com/bazelbuild/bazel-toolchains/releases/download/v5.1.2/rbe_configs_gen_windows_amd64.exe

RBE_CONFIGS_GEN_TOOL_PATH="./rbe_configs_gen_windows_amd64.exe"

# Actions on RBE will run under a dedicated docker image.
WINDOWS_RBE_DOCKER_IMAGE=us-docker.pkg.dev/grpc-testing/testing-images-public/rbe_windows2019@sha256:63aed074a2ca1bf5af45bb43b255d21d51882d7169ec57be7f0f5454ea5d2c98

# Bazel version used for configuring
# Needs to be one of the versions from bazel/supported_versions.txt chosen so that the result is compatible
# with other supported bazel versions.
BAZEL_VERSION=6.3.2

# Where to store the generated configs (relative to repo root)
CONFIG_OUTPUT_PATH=third_party/toolchains/rbe_windows_bazel_6.3.2_vs2019

# Delete old generated configs.
rm -rf "${REPO_ROOT}/${CONFIG_OUTPUT_PATH}"

# Pull the RBE windows docker image first.
# TOOD(jtattermusch): investigate why pulling a docker image on windows is extremely slow.
docker pull ${WINDOWS_RBE_DOCKER_IMAGE}

${RBE_CONFIGS_GEN_TOOL_PATH} \
    --bazel_version="${BAZEL_VERSION}" \
    --toolchain_container="${WINDOWS_RBE_DOCKER_IMAGE}" \
    --output_src_root="${REPO_ROOT}" \
    --output_config_path="${CONFIG_OUTPUT_PATH}" \
    --exec_os=windows \
    --target_os=windows \
    --generate_java_configs=false
