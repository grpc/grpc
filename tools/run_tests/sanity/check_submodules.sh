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
 df3ea785d8c30a9503321a3d35ee7d35808f190d third_party/abseil-cpp (heads/master)
 090faecb454fbd6e6e17a75ef8146acb037118d4 third_party/benchmark (v1.5.0)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty (remotes/origin/wide-14-g73594cd)
 412844d75b14b9090b58423fd5f5ed8c2fd80212 third_party/boringssl-with-bazel (remotes/origin/master-with-bazel)
 e982924acee7f7313b4baa4ee5ec000c5e373c30 third_party/cares/cares (cares-1_15_0)
 8dcc476be69437b505af181a6e8b167fdb101d7e third_party/envoy-api (heads/master)
 28f50e0fed19872e0fd50dd23ce2ee8cd759338e third_party/gflags (v2.2.0-5-g30dbc81)
 80ed4d0bbf65d57cc267dfc63bd2584557f11f9b third_party/googleapis (common-protos-1_3_1-915-g80ed4d0bb)
 c9ccac7cb7345901884aabf5d1a786cfa6e2f397 third_party/googletest (6e2f397)
 15ae750151ac9341e5945eb38f8982d59fb99201 third_party/libuv (v1.34.0)
 fde7cf7358ec7cd69e8db9be4f1fa6a5c431386a third_party/protobuf (v3.7.0-rc.2-1277-gfde7cf735)
 0f2bc6c0fdac9113e3863ea6e30e5b2bd33e3b40 third_party/protoc-gen-validate (v0.0.10)
 aecba11114cf1fac5497aeb844b6966106de3eb6 third_party/re2 (heads/master)
 e8cd3a4bb307e2c810cffff99f93e96e6d7fee85 third_party/udpa (heads/master)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
