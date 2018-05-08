#!/usr/bin/env bash
# Copyright 2017 gRPC authors.
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

# A temporary solution to give Kokoro credentials. 
# The file name 4321_grpc-testing-service needs to match auth_credential in 
# the build config.
# TODO: Use keystore.
mkdir -p ${KOKORO_KEYSTORE_DIR}
cp ${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json ${KOKORO_KEYSTORE_DIR}/4321_grpc-testing-service

temp_dir=$(mktemp -d)
ln -f "${KOKORO_GFILE_DIR}/bazel-release-0.12.0" ${temp_dir}/bazel
chmod 755 "${KOKORO_GFILE_DIR}/bazel-release-0.12.0"
export PATH="${temp_dir}:${PATH}"
# This should show ${temp_dir}/bazel
which bazel
chmod +x "${KOKORO_GFILE_DIR}/bazel_wrapper.py"

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA256 \
  test --jobs="200" \
  --test_timeout="3600,3600,3600,3600" \
  --test_output=errors  \
  --verbose_failures=true  \
  --keep_going  \
  --remote_accept_cached=true  \
  --spawn_strategy=remote  \
  --remote_local_fallback=false  \
  --remote_timeout=3600  \
  --strategy=Javac=remote  \
  --strategy=Closure=remote  \
  --genrule_strategy=remote  \
  --experimental_strict_action_env=true \
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/asci-toolchain/nosla-debian8-clang-msan@sha256:8f381d55c0456fb65821c90ada902c2584977e03a1eaca8fba8ce77e644c775b" }' \
  --define GRPC_PORT_ISOLATED_RUNTIME=1 \
  --copt=-gmlt \
  --strip=never \
  --cxxopt=--stdlib=libc++ \
  --copt=-fsanitize=memory \
  --linkopt=-fsanitize=memory \
  --copt=-fsanitize-memory-track-origins \
  --action_env=LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH \
  --host_crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/debian8_clang/0.3.0/bazel_0.10.0:toolchain \
  --crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/experimental/debian8_clang/0.3.0/bazel_0.10.0/msan:msan_experimental_toolchain \
  -- //test/...
