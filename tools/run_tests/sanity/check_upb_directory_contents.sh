#!/bin/sh
# Copyright 2026 gRPC authors.
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

set -e

export TEST=true

dir=$(dirname "$0")
grpc_root=$(realpath "${dir}/../../..")
cd "${grpc_root}"

diff_after_applying_patch=$(mktemp /tmp/upbXXXXXX)

# Apply our patch.
cd "third_party/protobuf"
git apply "${grpc_root}/third_party/upb.patch"
cd "${grpc_root}"

cleanup() {
  echo "restoring third_party/protobuf"
  cd "${grpc_root}/third_party/protobuf"
  git reset --hard
  rm "${diff_after_applying_patch}"
}

trap cleanup EXIT


if ! (git diff --no-index "third_party/protobuf/upb" "third_party/upb/upb" > "${diff_after_applying_patch}") ; then
  echo "third_party/upb has unexpected change. Diff contents:"
  cat "${diff_after_applying_patch}"
  exit 1
fi
