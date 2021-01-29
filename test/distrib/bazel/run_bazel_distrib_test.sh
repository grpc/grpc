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

cd "$(dirname "$0")"/../../..

shopt -s nullglob

EXCEPTIONS=(
  # iOS platform fails the analysis phase since there is no toolchain available
  # for it.
  "//src/objective-c/..."
  "//third_party/objective_c/..."

  # This could be a legitmate failure due to bitrot.
  "//src/proto/grpc/testing:test_gen_proto"

  # This appears to be a legitimately broken BUILD file. There's a reference to
  # a non-existent "link_dynamic_library.sh".
   "//third_party/toolchains/bazel_0.26.0_rbe_windows:all"
)

SUPPORTED_VERSIONS=(
  "1.2.1"
  "2.2.0"
  "3.7.2"
  "4.0.0"
)

for VERSION in "${SUPPORTED_VERSIONS[@]}"; do
    export OVERRIDE_BAZEL_VERSION="$VERSION"
    printf ' -%s' "${EXCEPTIONS[@]}" | xargs bazel build -- //...
done
