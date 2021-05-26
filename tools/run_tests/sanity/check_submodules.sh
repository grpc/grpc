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

git submodule | awk '{ print $2 " " $1 }' | sort > "$submodules"
cat << EOF | sort > "$want_submodules"
third_party/abseil-cpp 997aaf3a28308eba1b9156aa35ab7bca9688e9f6
third_party/benchmark 73d4d5e8d6d449fc8663765a42aa8aeeee844489
third_party/bloaty 73594cde8c9a52a102c4341c244c833aa61b9c06
third_party/boringssl-with-bazel 688fc5cf5428868679d2ae1072cad81055752068
third_party/cares/cares e982924acee7f7313b4baa4ee5ec000c5e373c30
third_party/envoy-api 18b54850c9b7ba29a4ab67cbd7ed7eab7b0bbdb2
third_party/googleapis 82944da21578a53b74e547774cf62ed31a05b841
third_party/googletest c9ccac7cb7345901884aabf5d1a786cfa6e2f397
third_party/libuv 15ae750151ac9341e5945eb38f8982d59fb99201
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/protobuf 436bd7880e458532901c58f4d9d1ea23fa7edd52
third_party/protoc-gen-validate 872b28c457822ed9c2a5405da3c33f386ac0e86f
third_party/re2 aecba11114cf1fac5497aeb844b6966106de3eb6
third_party/udpa cc1b757b3eddccaaaf0743cbb107742bb7e3ee4f
third_party/zlib cacf7f1d4e3d44d871b605da3b647f07d718623f
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
