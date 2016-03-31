#!/bin/bash

# Copyright 2016, Google Inc.
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

#
# Example usage:
#   tools/codegen/core/gen_load_balancing_proto.sh \
#     src/proto/grpc/lb/v0/load_balancer.proto

read -r -d '' COPYRIGHT <<'EOF'
/*
 *
 * Copyright <YEAR>, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

EOF

CURRENT_YEAR=$(date +%Y)
COPYRIGHT_FILE=$(mktemp)
echo "${COPYRIGHT/<YEAR>/$CURRENT_YEAR}" > $COPYRIGHT_FILE

set -ex
if [ $# -eq 0 ]; then
  echo "Usage: $0 <load_balancer.proto> [output dir]"
  exit 1
fi

readonly GRPC_ROOT=$PWD

OUTPUT_DIR="$GRPC_ROOT/src/core/ext/lb_policy/grpclb/proto/grpc/lb/v0"
if [ $# -eq 2 ]; then
  mkdir -p "$2"
  if [ $? != 0 ]; then
    echo "Error creating output directory $2"
    exit 2
  fi
  OUTPUT_DIR="$2"
fi

readonly EXPECTED_OPTIONS_FILE_PATH="${1%.*}.options"

if [[ ! -f "$1" ]]; then
  echo "Input proto file '$1' doesn't exist."
  exit 3
fi
if [[ ! -f "${EXPECTED_OPTIONS_FILE_PATH}" ]]; then
  echo "Expected nanopb options file '${EXPECTED_OPTIONS_FILE_PATH}' missing"
  exit 4
fi

readonly VENV_DIR=$(mktemp -d)
readonly VENV_NAME="nanopb-$(date '+%Y%m%d_%H%M%S_%N')"
pushd $VENV_DIR
virtualenv $VENV_NAME
. $VENV_NAME/bin/activate
popd

# this should be the same version as the submodule we compile against
# ideally we'd update this as a template to ensure that
pip install protobuf==3.0.0b2

pushd "$(dirname $1)" > /dev/null

protoc \
--plugin=protoc-gen-nanopb="$GRPC_ROOT/third_party/nanopb/generator/protoc-gen-nanopb" \
--nanopb_out='-T -L#include\ \"third_party/nanopb/pb.h\"'":$OUTPUT_DIR" \
"$(basename $1)"

readonly PROTO_BASENAME=$(basename $1 .proto)
sed -i "s:$PROTO_BASENAME.pb.h:src/core/ext/lb_policy/grpclb/proto/grpc/lb/v0/$PROTO_BASENAME.pb.h:g" \
    "$OUTPUT_DIR/$PROTO_BASENAME.pb.c"

# prepend copyright
TMPFILE=$(mktemp)
cat $COPYRIGHT_FILE "$OUTPUT_DIR/$PROTO_BASENAME.pb.c" > $TMPFILE
mv -v $TMPFILE "$OUTPUT_DIR/$PROTO_BASENAME.pb.c"
cat $COPYRIGHT_FILE "$OUTPUT_DIR/$PROTO_BASENAME.pb.h" > $TMPFILE
mv -v $TMPFILE "$OUTPUT_DIR/$PROTO_BASENAME.pb.h"

deactivate
rm -rf $VENV_DIR

popd > /dev/null
