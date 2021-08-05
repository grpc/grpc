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
third_party/boringssl-with-bazel bcc01b6c66b1c6fa2816b108e50a544b757fbd7b
third_party/cares/cares e982924acee7f7313b4baa4ee5ec000c5e373c30
third_party/envoy-api df3b1ab2773147f292c4f175f790c35448328161
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest c9ccac7cb7345901884aabf5d1a786cfa6e2f397
third_party/libuv 15ae750151ac9341e5945eb38f8982d59fb99201
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf 436bd7880e458532901c58f4d9d1ea23fa7edd52
third_party/protoc-gen-validate 59da36e59fef2267fc2b1849a05159e3ecdf24f3
third_party/re2 aecba11114cf1fac5497aeb844b6966106de3eb6
third_party/udpa 6414d713912e988471d192940b62bf552b11793a
third_party/zlib cacf7f1d4e3d44d871b605da3b647f07d718623f
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
