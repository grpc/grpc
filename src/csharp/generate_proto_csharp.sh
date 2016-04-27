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

# Regenerates gRPC service stubs from proto files.
set +e
cd $(dirname $0)/../..

PROTOC=bins/opt/protobuf/protoc
PLUGIN=protoc-gen-grpc=bins/opt/grpc_csharp_plugin
EXAMPLES_DIR=src/csharp/Grpc.Examples
HEALTHCHECK_DIR=src/csharp/Grpc.HealthCheck
TESTING_DIR=src/csharp/Grpc.IntegrationTesting

$PROTOC --plugin=$PLUGIN --csharp_out=$EXAMPLES_DIR --grpc_out=$EXAMPLES_DIR \
    -I src/proto/math src/proto/math/math.proto

$PROTOC --plugin=$PLUGIN --csharp_out=$HEALTHCHECK_DIR --grpc_out=$HEALTHCHECK_DIR \
    -I src/proto/grpc/health/v1 src/proto/grpc/health/v1/health.proto

$PROTOC --plugin=$PLUGIN --csharp_out=$TESTING_DIR --grpc_out=$TESTING_DIR \
    -I . src/proto/grpc/testing/{control,empty,messages,metrics,payloads,services,stats,test}.proto 
