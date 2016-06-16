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

GRPC_CPP_PLUGIN_PATH=`which grpc_cpp_plugin`

cd `$dirname $0`/..

protoc src/proto/grpc/lb/v1/load_balancer --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/lb/v1/load_balancer --cpp_out=./
protoc src/proto/grpc/testing/compiler_test --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/compiler_test --cpp_out=./
protoc src/proto/grpc/testing/control --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/control --cpp_out=./
protoc src/proto/grpc/testing/duplicate/echo_duplicate --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/duplicate/echo_duplicate --cpp_out=./
protoc src/proto/grpc/testing/echo --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/echo --cpp_out=./
protoc src/proto/grpc/testing/echo_messages --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/echo_messages --cpp_out=./
protoc src/proto/grpc/testing/empty --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/empty --cpp_out=./
protoc src/proto/grpc/testing/messages --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/messages --cpp_out=./
protoc src/proto/grpc/testing/metrics --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/metrics --cpp_out=./
protoc src/proto/grpc/testing/payloads --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/payloads --cpp_out=./
protoc src/proto/grpc/testing/services --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/services --cpp_out=./
protoc src/proto/grpc/testing/stats --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/stats --cpp_out=./
protoc src/proto/grpc/testing/test --grpc_out=./ --plugin=protoc-gen-grpc=$GRPC_CPP_PLUGIN_PATH
protoc src/proto/grpc/testing/test --cpp_out=./
