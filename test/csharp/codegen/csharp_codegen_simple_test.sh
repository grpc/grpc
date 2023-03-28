#!/bin/bash
# Copyright 2023 gRPC authors.
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

# Run this script via bazel test
# It expects that protoc and grpc_csharp_plugin have already been built.

# Simple test - compare generated output to expected files

set -eux

TESTNAME=simple

# protoc and grpc_csharp_plugin binaries are supplied as "data" in bazel
PROTOC=./external/com_google_protobuf/protoc
PLUGIN=./src/compiler/grpc_csharp_plugin

# where to find the test data
DATA_DIR=./test/csharp/codegen/${TESTNAME}/

# output directory for the generated files
PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

# run protoc and the plugin
$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc_out=${PROTO_OUT} \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/helloworld.proto

# log the files generated
ls -l ./proto_out

# Verify the output files exist
[ -e ${PROTO_OUT}/Helloworld.cs ] || {
    echo >&2 "missing generated output, expecting Helloworld.cs"
    exit 1
}
[ -e ${PROTO_OUT}/HelloworldGrpc.cs ] || {
    echo >&2 "missing generated output, expecting HelloworldGrpc"
    exit 1
}

DIFFCMD="diff --strip-trailing-cr"

# diff expected files
[ "`$DIFFCMD ${DATA_DIR}/expected/Helloworld.cs \
             ${PROTO_OUT}/Helloworld.cs`" ] && {
    echo >&2 "Generated code does not match for Helloworld.cs"
    exit 1
}
[ "`$DIFFCMD ${DATA_DIR}/expected/HelloworldGrpc.cs \
             ${PROTO_OUT}/HelloworldGrpc.cs`" ] && {
    echo >&2 "Generated code does not match for HelloworldGrpc.cs"
    exit 1
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin test: ${TESTNAME}: passed."
