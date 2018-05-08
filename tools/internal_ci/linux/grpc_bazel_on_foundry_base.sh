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

# TODO(adelez): implement size for test targets and change test_timeout back
"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA256 \
  test --jobs="200" \
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
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/cloud-marketplace/google/rbe-debian8@sha256:1ede2a929b44d629ec5abe86eee6d7ffea1d5a4d247489a8867d46cfde3e38bd" }' \
  --crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/debian8_clang/0.3.0/bazel_0.10.0:toolchain \
  --define GRPC_PORT_ISOLATED_RUNTIME=1 \
  $1 \
  -- //test/... || FAILED="true"

if [ "$UPLOAD_TEST_RESULTS" != "" ]
then
  python ./tools/run_tests/python_utils/upload_rbe_results.py
fi

if [ "$FAILED" != "" ]
then
  exit 1
fi
