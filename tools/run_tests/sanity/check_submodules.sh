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
 74d91756c11bc22f9b0108b94da9326f7f9e376f third_party/abseil-cpp (74d9175)
 090faecb454fbd6e6e17a75ef8146acb037118d4 third_party/benchmark (v1.5.0)
 73594cde8c9a52a102c4341c244c833aa61b9c06 third_party/bloaty (remotes/origin/wide-14-g73594cd)
 b29b21a81b32ec273f118f589f46d56ad3332420 third_party/boringssl (remotes/origin/chromium-stable)
 afc30d43eef92979b05776ec0963c9cede5fb80f third_party/boringssl-with-bazel (fips-20180716-116-gafc30d43e)
 e982924acee7f7313b4baa4ee5ec000c5e373c30 third_party/cares/cares (cares-1_15_0)
 c181f78882e54c0e5c63f332562ef6954ee7932f third_party/envoy-api (heads/master)
 28f50e0fed19872e0fd50dd23ce2ee8cd759338e third_party/gflags (v2.2.0-5-g30dbc81)
 80ed4d0bbf65d57cc267dfc63bd2584557f11f9b third_party/googleapis (common-protos-1_3_1-915-g80ed4d0bb)
 c9ccac7cb7345901884aabf5d1a786cfa6e2f397 third_party/googletest (6e2f397)
 6599cac0965be8e5a835ab7a5684bbef033d5ad0 third_party/libcxx (heads/release_60)
 9245d481eb3e890f708ff2d7dadf2a10c04748ba third_party/libcxxabi (heads/release_60)
 09745575a923640154bcf307fba8aedff47f240a third_party/protobuf (v3.7.0-rc.2-247-g09745575)
 e143189bf6f37b3957fb31743df6a1bcf4a8c685 third_party/protoc-gen-validate (v0.0.10)
 94324803a497c8f76dbc78df393ef629d3a9f3c3 third_party/udpa (heads/master)
 931bbecbd3230ae7f22efa5d203639facc47f719 third_party/upb (heads/master)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
