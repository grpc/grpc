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

git submodule | awk '{ print $1 }' | sort > "$submodules"
cat << EOF | awk '{ print $1 }' | sort > "$want_submodules"
 6f9d96a1f41439ac172ee2ef7ccd8edf0e5d068c third_party/abseil-cpp (heads/master)
 73d4d5e8d6d449fc8663765a42aa8aeeee844489 third_party/benchmark (v1.5.2)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty (remotes/origin/wide-14-g73594cd)
 1a7359455220f7010def8c63f7c7e041ce6707c6 third_party/boringssl-with-bazel (remotes/origin/master-with-bazel)
 e982924acee7f7313b4baa4ee5ec000c5e373c30 third_party/cares/cares (cares-1_15_0)
 18b54850c9b7ba29a4ab67cbd7ed7eab7b0bbdb2 third_party/envoy-api (remotes/origin/main)
 82944da21578a53b74e547774cf62ed31a05b841 third_party/googleapis (common-protos-1_3_1-915-g80ed4d0bb)
 c9ccac7cb7345901884aabf5d1a786cfa6e2f397 third_party/googletest (6e2f397)
 15ae750151ac9341e5945eb38f8982d59fb99201 third_party/libuv (v1.34.0)
 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89 third_party/opencensus-proto (v0.3.0)
 d7e943b8d2bc444a8c770644e73d090b486f8b37 third_party/protobuf (v3.15.2)
 872b28c457822ed9c2a5405da3c33f386ac0e86f third_party/protoc-gen-validate (v0.4.1)
 aecba11114cf1fac5497aeb844b6966106de3eb6 third_party/re2 (heads/master)
 cc1b757b3eddccaaaf0743cbb107742bb7e3ee4f third_party/udpa (heads/master)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
