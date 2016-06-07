#!/bin/bash
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

set -ex

readonly NANOPB_TMP_OUTPUT="$(mktemp -d)"
readonly PROTOBUF_INSTALL_PREFIX="$(mktemp -d)"

# install protoc version 3
pushd third_party/protobuf
./autogen.sh
./configure --prefix="$PROTOBUF_INSTALL_PREFIX"
make
make install
#ldconfig
popd

readonly PROTOC_BIN_PATH="$PROTOBUF_INSTALL_PREFIX/bin"
if [ ! -x "$PROTOBUF_INSTALL_PREFIX/bin/protoc" ]; then
  echo "Error: protoc not found in temp install dir '$PROTOBUF_INSTALL_PREFIX'"
  exit 1
fi

# stack up and change to nanopb's proto generator directory
pushd third_party/nanopb/generator/proto
export PATH="$PROTOC_BIN_PATH:$PATH"
make
# back to the root directory
popd

#
# Checks for load_balancer.proto
#
readonly LOAD_BALANCER_GRPC_OUTPUT_PATH='src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1'
# nanopb-compile the proto to a temp location
./tools/codegen/core/gen_nano_proto.sh \
  src/proto/grpc/lb/v1/load_balancer.proto \
  "$NANOPB_TMP_OUTPUT" \
  "$LOAD_BALANCER_GRPC_OUTPUT_PATH"

# compare outputs to checked compiled code
if ! diff -r $NANOPB_TMP_OUTPUT src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1; then
  echo "Outputs differ: $NANOPB_TMP_OUTPUT vs $LOAD_BALANCER_GRPC_OUTPUT_PATH"
  exit 2
fi
