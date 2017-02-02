#!/bin/sh

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


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
 f8a0efe03aa69b3336d8e228b37d4ccb17324b88 third_party/gflags (v2.2.0)
 c99458533a9b4c743ed51537e25989ea55944908 third_party/googletest (release-1.7.0)
 a428e42072765993ff674fda72863c9f1aa2d268 third_party/protobuf (v3.1.0-alpha-1)
 bcad91771b7f0bff28a1cac1981d7ef2b9bcef3c third_party/thrift (bcad917)
 50893291621658f355bc5b4d450a8d06a563053d third_party/zlib (v1.2.8)
EOF

diff -u $submodules $want_submodules

rm $submodules $want_submodules
