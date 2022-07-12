#!/usr/bin/env bash
# Copyright 2021 The gRPC authors.
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

if [ "$#" != "1" ] ; then
    echo "Must supply bazel version to be tested." >/dev/stderr
    exit 1
fi

VERSION="$1"

cd "$(dirname "$0")"/../../..

EXCLUDED_TARGETS=(
  # iOS platform fails the analysis phase since there is no toolchain available
  # for it.
  "-//src/objective-c/..."
  "-//third_party/objective_c/..."

  # This could be a legitmate failure due to bitrot.
  "-//src/proto/grpc/testing:test_gen_proto"

  # This appears to be a legitimately broken BUILD file. There's a reference to
  # a non-existent "link_dynamic_library.sh".
  "-//third_party/toolchains/rbe_windows_bazel_5.2.0_vs2019:all"

  # TODO(jtattermusch): add back once fixed
  "-//examples/android/binder/..."
)

TEST_DIRECTORIES=(
  "cpp"
  "python"
)

FAILED_TESTS=""

export OVERRIDE_BAZEL_VERSION="$VERSION"
# when running under bazel docker image, the workspace is read only.
export OVERRIDE_BAZEL_WRAPPER_DOWNLOAD_DIR=/tmp
bazel build -- //... "${EXCLUDED_TARGETS[@]}" || FAILED_TESTS="${FAILED_TESTS}Build "

for TEST_DIRECTORY in "${TEST_DIRECTORIES[@]}"; do
  pushd "test/distrib/bazel/$TEST_DIRECTORY/"

  bazel test --test_output=all //:all || FAILED_TESTS="${FAILED_TESTS}${TEST_DIRECTORY} Distribtest"

  popd
done

if [ "$FAILED_TESTS" != "" ]
then
  echo "Failed tests at version ${VERSION}: ${FAILED_TESTS}"
  exit 1
fi
