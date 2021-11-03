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

git submodule | awk '{ print $2 " " $1 }' | sort >"$submodules"
cat <<EOF | sort >"$want_submodules"
third_party/abseil-cpp 278e0a071885a22dcd2fd1b5576cc44757299343
third_party/benchmark 0baacde3618ca617da95375e0af13ce1baadea47
third_party/bloaty 60209eb1ccc34d5deefb002d1b7f37545204f7f2
third_party/boringssl-with-bazel 95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0
third_party/cares/cares e982924acee7f7313b4baa4ee5ec000c5e373c30
third_party/envoy-api 20b1b5fcee88a20a08b71051a961181839ec7268
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest c9ccac7cb7345901884aabf5d1a786cfa6e2f397
third_party/libuv d6ab6a7b3a7ae9f2fa3039c0bbe9b550260b1049
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf 0dab03ba7bc438d7ba3eac2b2c1eb39ed520f928
third_party/protoc-gen-validate 59da36e59fef2267fc2b1849a05159e3ecdf24f3
third_party/re2 8e08f47b11b413302749c0d8b17a1c94777495d5
third_party/xds cb28da3451f158a947dfc45090fe92b07b243bc1
third_party/zlib cacf7f1d4e3d44d871b605da3b647f07d718623f
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
