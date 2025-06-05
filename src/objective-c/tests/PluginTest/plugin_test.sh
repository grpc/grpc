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

# shellcheck disable=SC1090

set -ev

# --- begin runfiles.bash initialization v3 ---
# Copy-pasted from the Bazel Bash runfiles library v3.
set -uo pipefail; set +e; f=bazel_tools/tools/bash/runfiles/runfiles.bash
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null || \
  source "$0.runfiles/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  { echo>&2 "ERROR: cannot find $f"; exit 1; }; f=; set -e
# --- end runfiles.bash initialization v3 ---

# protoc and grpc_objective_c_plugin binaries are supplied as "data" in bazel
PROTOC=$(rlocation "$RLOCATIONPATH_PROTOC")
PLUGIN=$(rlocation "$RLOCATIONPATH_PLUGIN")

PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

$PROTOC \
    --plugin=protoc-gen-grpc="$PLUGIN" \
    --objc_out=${PROTO_OUT} \
    --grpc_out=${PROTO_OUT} \
    -I src/objective-c/tests/PluginTest \
    src/objective-c/tests/PluginTest/*.proto

# Verify the output proto filename
[ -e ${PROTO_OUT}/TestDashFilename.pbrpc.h ] || {
    echo >&2 "protoc outputs wrong filename."
    exit 1
}

# TODO(jtattermusch): rewrite the tests to make them more readable.
# Also, the way they are written, they need one extra command to run in order to
# clear $? after they run (see end of this script)
# Verify names of the imported protos in generated code don't contain dashes.
[ "`cat ${PROTO_OUT}/TestDashFilename.pbrpc.h |
    egrep '#import ".*\.pb(objc|rpc)\.h"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}
[ "`cat ${PROTO_OUT}/TestDashFilename.pbrpc.m |
    egrep '#import ".*\.pb(objc|rpc)\.h"$' |
    egrep '-'`" ] && {
    echo >&2 "protoc generated import with wrong filename."
    exit 1
}

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin tests passed."
