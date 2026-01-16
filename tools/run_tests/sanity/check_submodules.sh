#!/bin/sh

# Copyright 2015 gRPC authors.
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

cd "$(dirname "$0")/../../.."

submodules=$(mktemp /tmp/submXXXXXX)
want_submodules=$(mktemp /tmp/submXXXXXX)

git submodule | sed 's/+//g' | awk '{ print $2 " " $1 }' | sort >"$submodules"
cat <<EOF | sort >"$want_submodules"
third_party/abseil-cpp 76bb24329e8bf5f39704eb10d21b9a80befa7c81
third_party/benchmark 12235e24652fc7f809373e7c11a5f73c5763fc4c
third_party/bloaty 60209eb1ccc34d5deefb002d1b7f37545204f7f2
third_party/boringssl-with-bazel c63fadbde60a2224c22189d14c4001bbd2a3a629
third_party/cares/cares d3a507e920e7af18a5efb7f9f1d8044ed4750013
third_party/envoy-api 6ef568cf4a67362849911d1d2a546fd9f35db2ff
third_party/googleapis 2193a2bfcecb92b92aad7a4d81baa428cafd7dfd
third_party/googletest 52eb8108c5bdec04579160ae17225d66034bd723
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/opentelemetry-cpp ced79860f8c8a091a2eabfee6d47783f828a9b59
third_party/protobuf 74211c0dfc2777318ab53c2cd2c317a2ef9012de
third_party/protoc-gen-validate 7b06248484ceeaa947e93ca2747eccf336a88ecc
third_party/re2 0c5616df9c0aaa44c9440d87422012423d91c7d1
third_party/xds ee656c7534f5d7dc23d44dd611689568f72017a6
third_party/zlib f1f503da85d52e56aae11557b4d79a42bcaa2b86
EOF

if ! diff -u "$submodules" "$want_submodules"; then
  if [ "$1" = "--fix" ]; then
    while read -r path commit; do
      git submodule update --init "$path"
      (cd "$path" && git checkout "$commit")
    done <"$want_submodules"
    exit 0
  fi
  echo "Submodules are out of sync. Please update this script or run with --fix."
  exit 1
fi

rm "$submodules" "$want_submodules"
