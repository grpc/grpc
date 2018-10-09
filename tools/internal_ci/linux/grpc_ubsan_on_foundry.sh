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
ln -f "${KOKORO_GFILE_DIR}/bazel-rc-0.14.0rc5" ${temp_dir}/bazel
chmod 755 "${KOKORO_GFILE_DIR}/bazel-rc-0.14.0rc5"
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
  --define GRPC_PORT_ISOLATED_RUNTIME=1 \
  --copt=-gmlt \
  --strip=never \
  --copt=-fsanitize=undefined \
  --linkopt=-fsanitize=undefined \
  --crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/experimental/ubuntu16_04_clang/1.0/bazel_0.13.0/ubsan:toolchain \
  --action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1 \
  --extra_toolchains=@com_github_bazelbuild_bazeltoolchains//configs/ubuntu16_04_clang/1.0/bazel_0.13.0/cpp:cc-toolchain-clang-x86_64-default \
  --extra_execution_platforms=//third_party/toolchains:rbe_ubuntu1604 \
  --host_platform=//third_party/toolchains:rbe_ubuntu1604 \
  --platforms=//third_party/toolchains:rbe_ubuntu1604 \
  --cache_test_results=no \
  -- //test/... || FAILED="true"

# Sleep to let ResultStore finish writing results before querying
sleep 60
python ./tools/run_tests/python_utils/upload_rbe_results.py

if [ "$FAILED" != "" ]
then
  exit 1
fi
