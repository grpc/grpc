#!/bin/bash
# Copyright 2015 gRPC authors.
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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -ev

cd $(dirname $0)

# Run the tests server.

ROOT_DIR=../../..
BAZEL=$ROOT_DIR/tools/bazel
PROTOC=$ROOT_DIR/bazel-bin/external/com_google_protobuf/protoc
PLUGIN=$ROOT_DIR/bazel-bin/src/compiler/grpc_objective_c_plugin
RUNTIME_IMPORT_PREFIX=prefix/dir/

[ -f $PROTOC ] && [ -f $PLUGIN ] || {
    BAZEL build @com_google_protobuf//:protoc //src/compiler:grpc_objective_c_plugin
}

rm -rf RemoteTestClient/*pb*

$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --objc_out=RemoteTestClient \
    --grpc_out=runtime_import_prefix=$RUNTIME_IMPORT_PREFIX:RemoteTestClient \
    -I $ROOT_DIR \
    -I ../../../third_party/protobuf/src \
    $ROOT_DIR/src/objective-c/examples/RemoteTestClient/*.proto

# Verify the output proto filename
[ -e ./RemoteTestClient/src/objective-c/examples/RemoteTestClient/Test.pbrpc.m ] || {
    echo >&2 "protoc outputs wrong filename."
    exit 1
}

# Verify paths of protobuf WKTs in generated code contain runtime import prefix.
[ "`cat RemoteTestClient/src/objective-c/examples/RemoteTestClient/Test.pbrpc.m |
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"'GPBEmpty\.pbobjc\.h'`" ] || {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Verify paths of non WKTs protos in generated code don't contain runtime import prefix.
[ "`cat RemoteTestClient/src/objective-c/examples/RemoteTestClient/Test.pbrpc.m |
    egrep '.*\Messages.pbobjc.h"$' | 
    egrep '#import "'"${RUNTIME_IMPORT_PREFIX}"`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin option tests passed."
