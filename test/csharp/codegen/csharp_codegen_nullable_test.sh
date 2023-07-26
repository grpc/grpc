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

TESTNAME=nullable

# protoc and grpc_csharp_plugin binaries are supplied as "data" in bazel
PROTOC=./external/com_google_protobuf/protoc
PLUGIN=./src/compiler/grpc_csharp_plugin

# where to find the test data
DATA_DIR=./test/csharp/codegen/${TESTNAME}

# output directory for the generated files
PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

# run protoc and the plugin
$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc-csharp_out=${PROTO_OUT} \
    --grpc_out=${PROTO_OUT} \
    --grpc-csharp_opt=nullable_reference_types \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/nullabletest.proto

# log the files generated
ls -l ./proto_out

# Rather than doing a diff against a known file, just using grep to
# check for some of the code changes when "nullable_reference_types" is specified.
# This isn't bullet proof but does avoid tests breaking when other
# codegen changes are made.

# check "#nullable enabled" directive is present
nmatches=$(grep -c "#nullable enabled" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -ne 1 ]
then
    echo >&2 "Missing #nullable directive"
    exit 1
fi

# check annotation on "headers" parameter in the service methods
nmatches=$(grep -c -F "grpc::Metadata? headers" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -eq 0 ]
then
    echo >&2 "Missing annotation on headers parameter"
    exit 1
fi

# check annotation for null forgiving operator in AddMethod
nmatches=$(grep -c "AddMethod(.*null!" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -eq 0 ]
then
    echo >&2 "Missing null forgiving operator in AddMethod"
    exit 1
fi

# Run again without the nullable_reference_types options to check the code generated

rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --csharp_out=${PROTO_OUT} \
    --grpc-csharp_out=${PROTO_OUT} \
    --grpc_out=${PROTO_OUT} \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/nullabletest.proto

# log the files generated
ls -l ./proto_out

# check "#nullable enabled" directive is not present
nmatches=$(grep -c "#nullable enabled" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -ne 0 ]
then
    echo >&2 "Unexpected #nullable directive"
    exit 1
fi

# check annotation on "headers" parameter in the service methods
nmatches=$(grep -c -F "grpc::Metadata? headers" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -ne 0 ]
then
    echo >&2 "Unexpected annotation on headers parameter"
    exit 1
fi

# check annotation for null forgiving operator in AddMethod
nmatches=$(grep -c "AddMethod(.*null!" ${PROTO_OUT}/NullabletestGrpc.cs)
if [ "$nmatches" -ne 0 ]
then
    echo >&2 "Unexpected null forgiving operator in AddMethod"
    exit 1
fi

rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin test: ${TESTNAME}: passed."
