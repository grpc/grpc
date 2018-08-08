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
 cc4bed2d74f7c8717e31f9579214ab52a9c9c610 third_party/abseil-cpp (cc4bed2)
 5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8 third_party/benchmark (v1.2.0)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty (remotes/origin/wide-14-g73594cd)
 b29b21a81b32ec273f118f589f46d56ad3332420 third_party/boringssl (remotes/origin/chromium-stable)
 8149b351bf797bd80e063787886b7618f508e451 third_party/boringssl-with-bazel (version_for_cocoapods_10.0-434-g8149b351)
 3be1924221e1326df520f8498d704a5c4c8d0cce third_party/cares/cares (cares-1_13_0)
 30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e third_party/gflags (v2.2.0-5-g30dbc81)
 ec44c6c1675c25b9827aacd08c02433cccde7780 third_party/googletest (release-1.8.0)
 6599cac0965be8e5a835ab7a5684bbef033d5ad0 third_party/libcxx (heads/release_60)
 9245d481eb3e890f708ff2d7dadf2a10c04748ba third_party/libcxxabi (heads/release_60)
 48cb18e5c419ddd23d9badcfe4e9df7bde1979b2 third_party/protobuf (v3.6.0.1-37-g48cb18e5)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
