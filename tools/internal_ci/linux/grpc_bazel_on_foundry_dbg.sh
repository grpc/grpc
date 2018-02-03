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
mkdir -p ${KOKORO_KEYSTORE_DIR}
cp ${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json ${KOKORO_KEYSTORE_DIR}/4321_grpc-testing-service

mkdir -p /tmpfs/tmp/bazel-canary
ln -f "${KOKORO_GFILE_DIR}/bazel-canary" /tmpfs/tmp/bazel-canary/bazel
chmod 755 "${KOKORO_GFILE_DIR}/bazel-canary"
export PATH="/tmpfs/tmp/bazel-canary:${PATH}"
# This should show /tmpfs/tmp/bazel-canary/bazel
which bazel
chmod +x "${KOKORO_GFILE_DIR}/bazel_wrapper.py"

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA1 \
  test --jobs="50" \
  --test_timeout="300,450,1200,3600" \
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
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/asci-toolchain/nosla-debian8-clang-fl@sha256:aa20628a902f06a11a015caa94b0432eb60690de2d2525bd046b9eea046f5d8a" }' \
  --crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/debian8_clang/0.2.0/bazel_0.7.0:toolchain \
  --define GRPC_PORT_ISOLATED_RUNTIME=1 \
  -c dbg \
  -- //test/...
