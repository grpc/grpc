#!/bin/bash

# Copyright 2018 gRPC authors.
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

# Example usage:
#     gen_nano_proto.sh \
#     input_dir/handshaker.proto \
#     $PWD/output_dir \
#     alts_handshaker_service_dir
#
# Exit statuses:
# 1: Incorrect number of arguments
# 2: Input proto file (1st argument) doesn't exist or is not a regular file.
# 3: Output dir not an absolute path.
# 4: Couldn't create output directory (2nd argument).
#
# To run this script, virtualenv needs to be installed: pip install virtualenv
# TODO: Add a check to detect any changes made to ALTS handshake proto.

set -ex
if [[ $# -lt 2 ]] || [[ $# -gt 3 ]]; then
  echo "Usage: $0 <input.proto> <absolute path to output dir>" \
       "[alts handshaker service dir]"
  exit 1
fi

readonly INPUT_PROTO="$1"
readonly OUTPUT_DIR="$2"
readonly ALTS_HANDSHAKER_SERVICE_DIR="${3:-$OUTPUT_DIR}"
readonly NANOPB_VERSION="0.3.8"
readonly TMP_DIR="/tmp/nanopb-$NANOPB_VERSION-linux-x86/"
readonly PROTO_NAME=$(basename "$INPUT_PROTO" ".proto")
readonly OPTIONS="${TMP_DIR}${PROTO_NAME}.options"

# do sanity check.
if [[ ! -f "$INPUT_PROTO" ]]; then
  echo "Input proto file '$INPUT_PROTO' doesn't exist."
  exit 2
fi

if [[ "${OUTPUT_DIR:0:1}" != '/' ]]; then
  echo "The output directory must be an absolute path. Got '$OUTPUT_DIR'"
  exit 3
fi

mkdir -p "$OUTPUT_DIR"
if [[ $? != 0 ]]; then
  echo "Error creating output directory $OUTPUT_DIR"
  exit 4
fi

# create virtual environment.
readonly VENV_DIR=$(mktemp -d)
readonly VENV_NAME="nanopb-$(date '+%Y%m%d_%H%M%S_%N')"
pushd "$VENV_DIR"
virtualenv "$VENV_NAME"
. "$VENV_NAME/bin/activate"
popd

pip install protobuf==3.2.0

pushd "$(dirname "$INPUT_PROTO")" > "/dev/null"

# download nanopb from github.
wget \
"https://jpa.kapsi.fi/nanopb/download/nanopb-$NANOPB_VERSION-linux-x86.tar.gz"

mv "nanopb-$NANOPB_VERSION-linux-x86.tar.gz" "/tmp"

tar -xvf "/tmp/nanopb-$NANOPB_VERSION-linux-x86.tar.gz" -C "/tmp"

cp "$INPUT_PROTO" "$TMP_DIR"

# create and edit option file.
touch "$OPTIONS"
echo "$PROTO_NAME.* proto3:false" >> "$OPTIONS"
echo "$PROTO_NAME.proto no_unions:true" >> "$OPTIONS"
echo "grpc.gcp.StartServerHandshakeReq.handshake_parameters max_count:3" >> "$OPTIONS"

cd $TMP_DIR

# run protoc to generate *pb.h/c.
protoc \
  --plugin=protoc-gen-nanopb="generator/protoc-gen-nanopb" \
  --nanopb_out='-T -L#include\ \"third_party/nanopb/pb.h\"'":$OUTPUT_DIR" \
  "$PROTO_NAME.proto"

sed -i "s:$PROTO_NAME.pb.h:${ALTS_HANDSHAKER_SERVICE_DIR}$PROTO_NAME.pb.h:g" \
    "$OUTPUT_DIR/$PROTO_NAME.pb.c"

rm -r -f ../nanopb*

# exit virtual environment.
deactivate
rm -rf "$VENV_DIR"

popd > /dev/null
