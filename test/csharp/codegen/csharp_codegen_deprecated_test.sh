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

# shellcheck disable=SC1090

set -x

TESTNAME=deprecated

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

# protoc and grpc_csharp_plugin binaries are supplied as "data" in bazel
PROTOC=$(rlocation "$RLOCATIONPATH_PROTOC")
PLUGIN=$(rlocation "$RLOCATIONPATH_PLUGIN")

# where to find the test data
DATA_DIR=./test/csharp/codegen/${TESTNAME}

# output directory for the generated files
PROTO_OUT=./proto_out
rm -rf ${PROTO_OUT}
mkdir -p ${PROTO_OUT}

# run protoc and the plugin
$PROTOC \
    --plugin=protoc-gen-grpc="$PLUGIN" \
    --csharp_out=${PROTO_OUT} \
    --grpc_out=${PROTO_OUT} \
    -I ${DATA_DIR}/proto \
    ${DATA_DIR}/proto/depnothing.proto \
    ${DATA_DIR}/proto/depservice.proto \
    ${DATA_DIR}/proto/depmethod.proto

# log the files generated
ls -l ./proto_out

# Rather than doing a diff against a known file, just using grep to
# check for some of the code changes when "deprecated" is specified.
# This isn't bullet proof but does avoid tests breaking when other
# codegen changes are made.

# For depnothing.proto there should zero "ObsoleteAttribute" found
nmatches=$(grep -c ObsoleteAttribute ${PROTO_OUT}/DepnothingGrpc.cs || true)
if [ "$nmatches" -ne 0 ]
then
    echo >&2 "Unexpected ObsoleteAttribute in DepnothingGrpc.cs"
    exit 1
fi

# For depservice.proto need to check ObsoleteAttribute added to three classes.
# First check ObsoleteAttribute exists in output
nmatches=$(grep -c ObsoleteAttribute ${PROTO_OUT}/DepserviceGrpc.cs || true)
if [ "$nmatches" -eq 0 ]
then
    echo >&2 "Missing ObsoleteAttribute in DepserviceGrpc.cs"
    exit 1
fi

# capture context after ObsoleteAttribute for further checking
CTX=$(grep -A 2 ObsoleteAttribute ${PROTO_OUT}/DepserviceGrpc.cs || true)

# Check ObsoleteAttribute before class GreeterServiceLevelDep
nmatches=$(echo "$CTX" | grep -c "class GreeterServiceLevelDep$" || true)
if [ "$nmatches" -ne 1 ]
then
    echo >&2 "Missing ObsoleteAttribute on class GreeterServiceLevelDep"
    exit 1
fi
# Check ObsoleteAttribute before class GreeterServiceLevelDepBase
nmatches=$(echo "$CTX" | grep -c "class GreeterServiceLevelDepBase$" || true)
if [ "$nmatches" -ne 1 ]
then
    echo >&2 "Missing ObsoleteAttribute on class GreeterServiceLevelDepBase"
    exit 1
fi
# Check ObsoleteAttribute before class GreeterServiceLevelDepClient
nmatches=$(echo "$CTX" | grep -c "class GreeterServiceLevelDepClient" || true)
if [ "$nmatches" -ne 1 ]
then
    echo >&2 "Missing ObsoleteAttribute on class GreeterServiceLevelDepClient"
    exit 1
fi

# For depmethod.proto need to check ObsoleteAttribute added in five places for SayHello method.
# Check ObsoleteAttribute exists in output
nmatches=$(grep -c ObsoleteAttribute ${PROTO_OUT}/DepmethodGrpc.cs || true)
if [ "$nmatches" -eq 0 ]
then
    echo >&2 "Missing ObsoleteAttribute in DepmethodGrpc.cs"
    exit 1
fi
# Check ObsoleteAttribute before SayHello methods
nmatches=$(grep -A 2 ObsoleteAttribute ${PROTO_OUT}/DepmethodGrpc.cs | grep -c SayHello || true)
if [ "$nmatches" -ne 5 ]
then
    echo >&2 "Missing ObsoleteAttribute on SayHello method"
    exit 1
fi

# Run one extra command to clear $? before exiting the script to prevent
# failing even when tests pass.
echo "Plugin test: ${TESTNAME}: passed."
