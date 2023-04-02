#!/bin/bash
# Copyright 2022 The gRPC Authors
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
# It expects that protoc and grpc_objective_c_plugin have already been built.

set -ev

# protoc and grpc_objective_c_plugin binaries are supplied as "data" in bazel
PROTOC=./external/com_google_protobuf/protoc
PLUGIN=./src/compiler/grpc_objective_c_plugin
WELL_KNOWN_PROTOS_PATH=external/com_google_protobuf/src
RUNTIME_IMPORT_PREFIX=prefix/dir/

PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --objc_out=${PROTO_OUT} \
    --grpc_out=grpc_local_import_prefix=$RUNTIME_IMPORT_PREFIX,runtime_import_prefix=$RUNTIME_IMPORT_PREFIX:${PROTO_OUT} \
    -I . \
    -I ${WELL_KNOWN_PROTOS_PATH} \
    src/objective-c/tests/RemoteTestClient/*.proto

# TODO(jtattermusch): rewrite the tests to make them more readable.
# Also, the way they are written, they need one extra command to run in order to
# clear $? after they run (see end of this script)

# Verify the "runtime_import_prefix" option
# Verify the output proto filename
[ -e ${PROTO_OUT}/src/objective-c/tests/RemoteTestClient/Test.pbrpc.m ] || {
    echo >&2 "protoc outputs wrong filename."
    exit 1
}

# Verify paths of protobuf WKTs in generated code contain runtime import prefix.
[ "`cat ${PROTO_OUT}/src/objective-c/tests/RemoteTestClient/Test.pbrpc.m |
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"'GPBEmpty\.pbobjc\.h'`" ] || {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Verify paths of non WKTs protos in generated code don't contain runtime import prefix.
[ "`cat ${PROTO_OUT}/src/objective-c/tests/RemoteTestClient/Test.pbrpc.m |
    egrep '.*\Messages.pbobjc.h"$' | 
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Verify the "grpc_local_import_directory" option
# Verify system files are imported in a "local" way in header files.
[ "`cat ${PROTO_OUT}/src/objective-c/tests/RemoteTestClient/Test.pbrpc.h |
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"'/ProtoRPC/.*\.h'`"] || {
    echo >&2 "grpc system files should be imported with full paths."    
}

# Verify system files are imported in a "local" way in source files.
[ "`cat ${PROTO_OUT}/src/objective-c/tests/RemoteTestClient/Test.pbrpc.m |
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"'/ProtoRPC/.*\.h'`"] || {
    echo >&2 "grpc system files should be imported with full paths."    
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin option tests passed."
