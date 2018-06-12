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
ln -f "${KOKORO_GFILE_DIR}/bazel-rc-0.14.0rc5" ${temp_dir}/bazel
chmod 755 "${KOKORO_GFILE_DIR}/bazel-rc-0.14.0rc5"
export PATH="${temp_dir}:${PATH}"
# This should show ${temp_dir}/bazel
which bazel
chmod +x "${KOKORO_GFILE_DIR}/bazel_wrapper.py"

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

COMMON_FLAGS='--verbose_failures=true
--keep_going
--remote_accept_cached=true
--remote_local_fallback=false
--remote_timeout=3600
--strategy=Closure=remote
--experimental_strict_action_env=true
--crosstool_top=@com_github_bazelbuild_bazeltoolchains//configs/ubuntu16_04_clang/1.0/bazel_0.13.0/default:toolchain
--define GRPC_PORT_ISOLATED_RUNTIME=1
--action_env=BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN=1
--extra_toolchains=@com_github_bazelbuild_bazeltoolchains//configs/ubuntu16_04_clang/1.0/bazel_0.13.0/cpp:cc-toolchain-clang-x86_64-default
--extra_execution_platforms=@com_github_bazelbuild_bazeltoolchains//configs/ubuntu16_04_clang/1.0:rbe_ubuntu1604
--copt=-gmlt
--strip=never
--copt=-fsanitize=thread
--linkopt=-fsanitize=thread'

# Build remotely first
"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA256 \
  build --jobs="200" \
  --spawn_strategy=remote  \
  --strategy=Javac=remote  \
  --genrule_strategy=remote  \
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:59bf0e191a6b5cc1ab62c2224c810681d1326bad5a27b1d36c9f40113e79da7f" }' \
  $COMMON_FLAGS \
  -- //test/... || FAILED="true"

# Run all test remotely except combiner_test
"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA256 \
  test --jobs="200" \
  --test_output=errors  \
  --spawn_strategy=remote  \
  --strategy=Javac=remote  \
  --genrule_strategy=remote  \
  --experimental_docker_privileged  \
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:59bf0e191a6b5cc1ab62c2224c810681d1326bad5a27b1d36c9f40113e79da7f" }' \
  --test_timeout=3600  \
  $COMMON_FLAGS \
  -- //test/... -//test/core/iomgr:combiner_test || FAILED="true"

if [ "$UPLOAD_TEST_RESULTS" != "" ]
then
  # Sleep to let ResultStore finish writing results before querying
  sleep 60
  python ./tools/run_tests/python_utils/upload_rbe_results.py
fi

# Run combiner_test in local docker container due to lack of memory on Foundry.
"${KOKORO_GFILE_DIR}/bazel_wrapper.py" \
  --host_jvm_args=-Dbazel.DigestFunction=SHA256 \
  test --test_output=errors  \
  --spawn_strategy=docker  \
  --strategy=Javac=docker  \
  --genrule_strategy=docker  \
  --define EXECUTOR=remote  \
  --experimental_docker_privileged  \
  --experimental_remote_platform_override='properties:{name:"container-image" value:"docker://gcr.io/cloud-marketplace/google/rbe-ubuntu16-04@sha256:59bf0e191a6b5cc1ab62c2224c810681d1326bad5a27b1d36c9f40113e79da7f" }' \
  --test_timeout=3600  \
  $COMMON_FLAGS \
  -- //test/core/iomgr:combiner_test || FAILED="true"

if [ "$UPLOAD_TEST_RESULTS" != "" ]
then
  # Sleep to let ResultStore finish writing results before querying
  sleep 60
  python ./tools/run_tests/python_utils/upload_rbe_results.py
fi

if [ "$FAILED" != "" ]
then
  exit 1
fi
