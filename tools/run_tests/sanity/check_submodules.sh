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
 5b7683f49e1e9223cf9927b24f6fd3d6bd82e3f8 third_party/benchmark (v1.2.0)
 70ef9596bbcc11353b9bb8d4e91478694dd21439 third_party/boringssl (fips-20170615-704-g70ef9596)
 dcd3e6e6ecddf059adb48fca45bc7346a108bdd9 third_party/boringssl-with-bazel (version_for_cocoapods_10.0-369-gdcd3e6e6)
 30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e third_party/gflags (v2.2.0)
 ec44c6c1675c25b9827aacd08c02433cccde7780 third_party/googletest (release-1.8.0)
 b5fbb742af122b565925987e65c08957739976a7 third_party/protobuf (v3.5.2)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
 3be1924221e1326df520f8498d704a5c4c8d0cce third_party/cares/cares (cares-1_13_0)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty
 cc4bed2d74f7c8717e31f9579214ab52a9c9c610 third_party/abseil-cpp
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
