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

git submodule | sed 's/+//g' | awk '{ print $2 " " $1 }' | sort >"$submodules"
cat <<EOF | sort >"$want_submodules"
third_party/abseil-cpp 29bf8085f3bf17b84d30e34b3d7ff8248fda404e
third_party/benchmark 344117638c8ff7e239044fd0fa7085839fc03021
third_party/bloaty 60209eb1ccc34d5deefb002d1b7f37545204f7f2
third_party/boringssl-with-bazel 2ff4b968a7e0cfee66d9f151cb95635b43dc1d5b
third_party/cares/cares 6360e96b5cf8e5980c887ce58ef727e53d77243a
third_party/envoy-api 9d6ffa70677c4dbf23f6ed569676206c4e2edff4
third_party/googleapis 2f9af297c84c55c8b871ba4495e01ade42476c92
third_party/googletest 2dd1c131950043a8ad5ab0d2dda0e0970596586a
third_party/opencensus-proto 4aa53e15cbf1a47bc9087e6cfdca214c1eea4e89
third_party/opentelemetry 60fa8754d890b5c55949a8c68dcfd7ab5c2395df
third_party/protobuf 7f94235e552599141950d7a4a3eaf93bc87d1b22
third_party/protoc-gen-validate fab737efbb4b4d03e7c771393708f75594b121e4
third_party/re2 0c5616df9c0aaa44c9440d87422012423d91c7d1
third_party/xds e9ce68804cb4e64cab5a52e3c8baf840d4ff87b7
third_party/zlib 09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851
EOF

diff -u "$submodules" "$want_submodules"

rm "$submodules" "$want_submodules"
