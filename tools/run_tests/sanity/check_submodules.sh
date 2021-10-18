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
third_party/abseil-cpp 997aaf3a28308eba1b9156aa35ab7bca9688e9f6
third_party/benchmark 73d4d5e8d6d449fc8663765a42aa8aeeee844489
third_party/bloaty 73594cde8c9a52a102c4341c244c833aa61b9c06
third_party/boringssl-with-bazel 95b3ed1b01f2ef1d72fed290ed79fe1b0e7dafc0
third_party/cares/cares e982924acee7f7313b4baa4ee5ec000c5e373c30
third_party/envoy-api 20b1b5fcee88a20a08b71051a961181839ec7268
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest c9ccac7cb7345901884aabf5d1a786cfa6e2f397
third_party/libuv 02a9e1be252b623ee032a3137c0b0c94afbe6809
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf 909a0f36a10075c4b4bc70fdee2c7e32dd612a72
third_party/protoc-gen-validate 59da36e59fef2267fc2b1849a05159e3ecdf24f3
third_party/re2 8e08f47b11b413302749c0d8b17a1c94777495d5
third_party/xds cb28da3451f158a947dfc45090fe92b07b243bc1
third_party/zlib cacf7f1d4e3d44d871b605da3b647f07d718623f
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
