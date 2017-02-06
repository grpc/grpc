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

set +e
cd $(dirname $0)/../../..

protoc --proto_path=src/proto/math \
       --php_out=src/php/tests/generated_code \
       --grpc_out=src/php/tests/generated_code \
       --plugin=protoc-gen-grpc=bins/opt/grpc_php_plugin \
       src/proto/math/math.proto

# replace the Empty message with EmptyMessage
# because Empty is a PHP reserved word
output_file=$(mktemp)
sed 's/message Empty/message EmptyMessage/g' \
  src/proto/grpc/testing/empty.proto > $output_file
mv $output_file ./src/proto/grpc/testing/empty.proto
sed 's/grpc\.testing\.Empty/grpc\.testing\.EmptyMessage/g' \
  src/proto/grpc/testing/test.proto > $output_file
mv $output_file ./src/proto/grpc/testing/test.proto

protoc --proto_path=. \
       --php_out=src/php/tests/interop \
       --grpc_out=src/php/tests/interop \
       --plugin=protoc-gen-grpc=bins/opt/grpc_php_plugin \
       src/proto/grpc/testing/messages.proto \
       src/proto/grpc/testing/empty.proto \
       src/proto/grpc/testing/test.proto

# change it back
sed 's/message EmptyMessage/message Empty/g' \
  src/proto/grpc/testing/empty.proto > $output_file
mv $output_file ./src/proto/grpc/testing/empty.proto
sed 's/grpc\.testing\.EmptyMessage/grpc\.testing\.Empty/g' \
  src/proto/grpc/testing/test.proto > $output_file
mv $output_file ./src/proto/grpc/testing/test.proto

