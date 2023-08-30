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

set -x

TESTNAME=basenamespace

# protoc and grpc_csharp_plugin binaries are supplied as "data" in bazel
PROTOC=./external/com_google_protobuf/protoc
PLUGIN=./src/compiler/grpc_csharp_plugin

# where to find the test data
DATA_DIR=./test/csharp/codegen/${TESTNAME}

# output directory for the generated files
PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

# run protoc and the plugin specifying the base_namespace options
$PROTOC \
    --plugin=protoc-gen-grpc-csharp=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc-csharp_out=${PROTO_OUT} \
    --csharp_opt=base_namespace=Example \
    --grpc-csharp_opt=base_namespace=Example \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/namespacetest.proto

# log the files generated
ls -lR ./proto_out

# Verify the output files exist in the right location.
# The base_namespace option does not change the generated code just
# the location of the files. Contents are not checked in this test.

# The C# namespace option in the proto file of "Example.V1.CodegenTest"
# combined with the command line options above should mean the generated files
# are created in the output directory "V1/CodegenTest"

# First file is generated by protoc.
[ -e ${PROTO_OUT}/V1/CodegenTest/Namespacetest.cs ] || {
    echo >&2 "missing generated output, expecting V1/CodegenTest/Namespacetest.cs"
    exit 1
}

# Second file is generated by the plugin.
[ -e ${PROTO_OUT}/V1/CodegenTest/NamespacetestGrpc.cs ] || {
    echo >&2 "missing generated output, expecting V1/CodegenTest/NamespacetestGrpc.cs"
    exit 1
}

# Run again with base_namespace option set to empty value to check that the files
# are created under a full directory structure defined by the namespace
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

$PROTOC \
    --plugin=protoc-gen-grpc-csharp=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc-csharp_out=${PROTO_OUT} \
    --csharp_opt=base_namespace= \
    --grpc-csharp_opt=base_namespace= \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/namespacetest.proto

# log the files generated
ls -lR ./proto_out

# Verify the output files exist in the right location.

# The C# namespace option in the proto file of "Example.V1.CodegenTest"
# combined with the command line options above should mean the generated files
# are created in the output directory "Example/V1/CodegenTest"

# First file is generated by protoc.
[ -e ${PROTO_OUT}/Example/V1/CodegenTest/Namespacetest.cs ] || {
    echo >&2 "missing generated output, expecting Example/V1/CodegenTest/Namespacetest.cs"
    exit 1
}

# Second file is generated by the plugin.
[ -e ${PROTO_OUT}/Example/V1/CodegenTest/NamespacetestGrpc.cs ] || {
    echo >&2 "missing generated output, expecting Example/V1/CodegenTest/NamespacetestGrpc.cs"
    exit 1
}

# Run again without the base_namespace options to check the files are created
# in the root of the output directory

rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

$PROTOC \
    --plugin=protoc-gen-grpc-csharp=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc-csharp_out=${PROTO_OUT} \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/namespacetest.proto

ls -lR ./proto_out

[ -e ${PROTO_OUT}/Namespacetest.cs ] || {
    echo >&2 "missing generated output, expecting Namespacetest.cs"
    exit 1
}

[ -e ${PROTO_OUT}/NamespacetestGrpc.cs ] || {
    echo >&2 "missing generated output, expecting NamespacetestGrpc.cs"
    exit 1
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin test: ${TESTNAME}: passed."
