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

[ -f $PROTOC ] && [ -f $PLUGIN ] || {
    BAZEL build @com_google_protobuf//:protoc //src/compiler:grpc_objective_c_plugin
}

rm -rf PluginTest/*pb*

$PROTOC \
    --plugin=protoc-gen-grpc=$PLUGIN \
    --objc_out=PluginTest \
    --grpc_out=PluginTest \
    -I PluginTest \
    -I ../../../third_party/protobuf/src \
    PluginTest/*.proto

# Verify the output proto filename
[ -e ./PluginTest/TestDashFilename.pbrpc.h ] || {
    echo >&2 "protoc outputs wrong filename."
    exit 1
}

# TODO(jtattermusch): rewrite the tests to make them more readable.
# Also, the way they are written, they need one extra command to run in order to
# clear $? after they run (see end of this script)
# Verify names of the imported protos in generated code don't contain dashes.
[ "`cat PluginTest/TestDashFilename.pbrpc.h |
    egrep '#import ".*\.pb(objc|rpc)\.h"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}
[ "`cat PluginTest/TestDashFilename.pbrpc.m |
    egrep '#import ".*\.pb(objc|rpc)\.h"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin tests passed."
