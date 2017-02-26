#!/bin/sh

# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
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
 78684e5b222645828ca302e56b40b9daff2b2d27 third_party/boringssl (78684e5)
 886e7d75368e3f4fab3f4d0d3584e4abfc557755 third_party/boringssl-with-bazel (version_for_cocoapods_7.0-857-g886e7d7)
 30dbc81fb5ffdc98ea9b14b1918bfe4e8779b26e third_party/gflags (v2.2.0)
 c99458533a9b4c743ed51537e25989ea55944908 third_party/googletest (release-1.7.0)
 a428e42072765993ff674fda72863c9f1aa2d268 third_party/protobuf (v3.1.0-alpha-1)
 bcad91771b7f0bff28a1cac1981d7ef2b9bcef3c third_party/thrift (bcad917)
 50893291621658f355bc5b4d450a8d06a563053d third_party/zlib (v1.2.8)
EOF

diff -u $submodules $want_submodules

rm $submodules $want_submodules
