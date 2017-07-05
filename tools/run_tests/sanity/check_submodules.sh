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

cd `dirname $0`/../../..

submodules=`mktemp /tmp/submXXXXXX`
want_submodules=`mktemp /tmp/submXXXXXX`

git submodule | awk '{ print $1 }' | sort > $submodules
cat << EOF | awk '{ print $1 }' | sort > $want_submodules
 44c25c892a6229b20db7cd9dc05584ea865896de third_party/benchmark (v0.1.0-343-g44c25c8)
 be2ee342d3781ddb954f91f8a7e660c6f59e87e5 third_party/boringssl (heads/chromium-stable)
 886e7d75368e3f4fab3f4d0d3584e4abfc557755 third_party/boringssl-with-bazel (version_for_cocoapods_7.0-857-g886e7d7)
 30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e third_party/gflags (v2.2.0)
 ec44c6c1675c25b9827aacd08c02433cccde7780 third_party/googletest (release-1.8.0)
 a6189acd18b00611c1dc7042299ad75486f08a1a third_party/protobuf (v3.3.0)
 cacf7f1d4e3d44d871b605da3b647f07d718623f third_party/zlib (v1.2.11)
 7691f773af79bf75a62d1863fd0f13ebf9dc51b1 third_party/cares/cares (1.12.0)
EOF

diff -u $submodules $want_submodules

rm $submodules $want_submodules
