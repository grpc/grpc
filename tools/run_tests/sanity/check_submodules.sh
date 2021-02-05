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
 090faecb454fbd6e6e17a75ef8146acb037118d4 third_party/benchmark (v1.5.0)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty (remotes/origin/wide-14-g73594cd)
 29c6e0e27268f5a43e039cd2ed4e849d6b736fc1 third_party/boringssl-with-bazel (remotes/origin/master-with-bazel)
 e982924acee7f7313b4baa4ee5ec000c5e373c30 third_party/cares/cares (cares-1_15_0)
 9edfeb841b0f8e54816f0b81949f79104072072c third_party/envoy-api (remotes/origin/main)
 82944da21578a53b74e547774cf62ed31a05b841 third_party/googleapis (common-protos-1_3_1-915-g80ed4d0bb)
 c9ccac7cb7345901884aabf5d1a786cfa6e2f397 third_party/googletest (6e2f397)
 15ae750151ac9341e5945eb38f8982d59fb99201 third_party/libuv (v1.34.0)
 19fb89416f3fdc2d6668f3738f444885575285bc third_party/protobuf (v3.7.0-rc.2-1277-gfde7cf735)
 872b28c457822ed9c2a5405da3c33f386ac0e86f third_party/protoc-gen-validate (v0.4.1)
 aecba11114cf1fac5497aeb844b6966106de3eb6 third_party/re2 (heads/master)
 cc1b757b3eddccaaaf0743cbb107742bb7e3ee4f third_party/udpa (heads/master)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
