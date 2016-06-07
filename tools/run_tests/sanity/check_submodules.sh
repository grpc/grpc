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
 c880e42ba1c8032d4cdde2aba0541d8a9d9fa2e9 third_party/boringssl (heads/2661)
 05b155ff59114735ec8cd089f669c4c3d8f59029 third_party/gflags (v2.1.0-45-g05b155f)
 c99458533a9b4c743ed51537e25989ea55944908 third_party/googletest (release-1.7.0)
 f8ac463766281625ad710900479130c7fcb4d63b third_party/nanopb (nanopb-0.3.4-29-gf8ac463)
 3470b6895aa659b7559ed678e029a5338e535f14 third_party/protobuf (v3.0.0-beta-2-441-g3470b68)
 50893291621658f355bc5b4d450a8d06a563053d third_party/zlib (v1.2.8)
EOF

diff -u $submodules $want_submodules

rm $submodules $want_submodules

