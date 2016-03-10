#!/bin/bash
# Copyright 2015-2016, Google Inc.
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

set -ex

apt-get install -y autoconf automake libtool curl python-virtualenv

readonly NANOPB_TMP_OUTPUT="$(mktemp -d)"

# install protoc version 3
pushd third_party/protobuf
./autogen.sh
./configure
make
make install
ldconfig
popd

if [ ! -x "/usr/local/bin/protoc" ]; then
  echo "Error: protoc not found in path"
  exit 1
fi
readonly PROTOC_PATH='/usr/local/bin'
# stack up and change to nanopb's proto generator directory
pushd third_party/nanopb/generator/proto
PATH="$PROTOC_PATH:$PATH" make

# back to the root directory
popd


# nanopb-compile the proto to a temp location
PATH="$PROTOC_PATH:$PATH" ./tools/codegen/core/gen_load_balancing_proto.sh \
  src/proto/grpc/lb/v0/load_balancer.proto \
  $NANOPB_TMP_OUTPUT

# compare outputs to checked compiled code
if ! diff -r $NANOPB_TMP_OUTPUT src/core/proto/grpc/lb/v0; then
  echo "Outputs differ: $NANOPB_TMP_OUTPUT vs src/core/proto/grpc/lb/v0"
  exit 2
fi
