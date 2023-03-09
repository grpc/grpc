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
third_party/abseil-cpp 78be63686ba732b25052be15f8d6dee891c05749
third_party/benchmark 361e8d1cfe0c6c36d30b39f1b61302ece5507320
third_party/bloaty 60209eb1ccc34d5deefb002d1b7f37545204f7f2
third_party/boringssl-with-bazel 85db207a482ae4f91f83a6a70d432b9121e48d2d
third_party/cares/cares 6654436a307a5a686b008c1d4c93b0085da6e6d8
third_party/envoy-api 5962b1204f4b7b0a2ed7622d0149727f602ae74c
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest 0e402173c97aea7a00749e825b194bfede4f2e45
third_party/libuv 02a9e1be252b623ee032a3137c0b0c94afbe6809
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c
third_party/re2 0c5616df9c0aaa44c9440d87422012423d91c7d1
third_party/xds 06c439db220b89134a8a49bad41994560d6537c6
third_party/zlib 04f42ceca40f73e2978b50e93806c2a18c1281fc
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
