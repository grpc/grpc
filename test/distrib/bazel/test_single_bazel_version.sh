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
ROOT_DIR="$(pwd)"

EXCLUDED_TARGETS=(
  # iOS platform fails the analysis phase since there is no toolchain available
  # for it.
  "-//src/objective-c/..."
  "-//third_party/objective_c/..."

  # This could be a legitmate failure due to bitrot.
  "-//src/proto/grpc/testing:test_gen_proto"

  # This appears to be a legitimately broken BUILD file. There's a reference to
  # a non-existent "link_dynamic_library.sh".
  "-//third_party/toolchains/bazel_0.26.0_rbe_windows:all"

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

# Picks the Bazel wrapper that will be used throughout the tests. This Bazel
# wrapper will read above OVERRIDE_BAZEL_VERSION and
# OVERRIDE_BAZEL_WRAPPER_DOWNLOAD_DIR flags to download the correct version of
# Bazel. By pining the Bazel wrapper, we can avoid the uncertainty of which
# Bazel is running.
BAZEL="${ROOT_DIR}/tools/bazel"

function check_bazel_version() {
  if ! (${BAZEL} version | grep -q "${VERSION}"); then
    echo "Incorrect Bazel version! Want=${VERSION} Seen=$($BAZEL version)"
    exit 1
  fi
}

# validate the Bazel version
check_bazel_version

# stop the Bazel local server to clear caches
$BAZEL shutdown

# test building all targets
$BAZEL build --repository_cache="" -- //... "${EXCLUDED_TARGETS[@]}" || FAILED_TESTS="${FAILED_TESTS}Build "

for TEST_DIRECTORY in "${TEST_DIRECTORIES[@]}"; do
  pushd "test/distrib/bazel/$TEST_DIRECTORY/"
  # stop the Bazel local server to clear caches
  $BAZEL shutdown
  # validate the Bazel version again, since we have a different WORKSPACE file
  check_bazel_version
  $BAZEL test --repository_cache="" --cache_test_results=no --test_output=all //:all || FAILED_TESTS="${FAILED_TESTS}${TEST_DIRECTORY} Distribtest"

  popd
done

if [ "$FAILED_TESTS" != "" ]
then
  echo "Failed tests at version ${VERSION}: ${FAILED_TESTS}"
  exit 1
fi
