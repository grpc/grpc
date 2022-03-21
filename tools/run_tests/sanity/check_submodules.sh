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
third_party/abseil-cpp 215105818dfde3174fe799600bb0f3cae233d0bf
third_party/benchmark 0baacde3618ca617da95375e0af13ce1baadea47
third_party/bloaty 60209eb1ccc34d5deefb002d1b7f37545204f7f2
third_party/boringssl-with-bazel b9232f9e27e5668bc0414879dcdedb2a59ea75f2
third_party/cares/cares 6654436a307a5a686b008c1d4c93b0085da6e6d8
third_party/envoy-api ff47e0a4bd03c0580305f9b138cc7937b63f7900
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest 0e402173c97aea7a00749e825b194bfede4f2e45
third_party/libuv 02a9e1be252b623ee032a3137c0b0c94afbe6809
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf 22d0e265de7d2b3d2e9a00d071313502e7d4cccf
third_party/protoc-gen-validate 59da36e59fef2267fc2b1849a05159e3ecdf24f3
third_party/re2 8e08f47b11b413302749c0d8b17a1c94777495d5
third_party/xds cb28da3451f158a947dfc45090fe92b07b243bc1
third_party/zlib cacf7f1d4e3d44d871b605da3b647f07d718623f
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
