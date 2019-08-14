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

set -ex

readonly NANOPB_HEALTH_TMP_OUTPUT="$(mktemp -d)"
readonly NANOPB_TMP_OUTPUT="$(mktemp -d)"
readonly PROTOBUF_INSTALL_PREFIX="$(mktemp -d)"

# install protoc version 3
pushd third_party/protobuf
./autogen.sh
./configure --prefix="$PROTOBUF_INSTALL_PREFIX"
make -j 8
make install
#ldconfig
popd

readonly PROTOC_BIN_PATH="$PROTOBUF_INSTALL_PREFIX/bin"
if [ ! -x "$PROTOBUF_INSTALL_PREFIX/bin/protoc" ]; then
  echo "Error: protoc not found in temp install dir '$PROTOBUF_INSTALL_PREFIX'"
  exit 1
fi

# stack up and change to nanopb's proto generator directory
pushd third_party/nanopb/generator/proto
export PATH="$PROTOC_BIN_PATH:$PATH"
make -j 8
# back to the root directory
popd

#
# checks for health.proto
#
readonly HEALTH_GRPC_OUTPUT_PATH='src/core/ext/filters/client_channel/health'
# nanopb-compile the proto to a temp location
./tools/codegen/core/gen_nano_proto.sh \
  src/proto/grpc/health/v1/health.proto \
  "$NANOPB_HEALTH_TMP_OUTPUT" \
  "$HEALTH_GRPC_OUTPUT_PATH"
# compare outputs to checked compiled code
for NANOPB_OUTPUT_FILE in $NANOPB_HEALTH_TMP_OUTPUT/*.pb.*; do
  if ! diff "$NANOPB_OUTPUT_FILE" "${HEALTH_GRPC_OUTPUT_PATH}/$(basename $NANOPB_OUTPUT_FILE)"; then
    echo "Outputs differ: $NANOPB_HEALTH_TMP_OUTPUT vs $HEALTH_GRPC_OUTPUT_PATH"
    exit 2
  fi
done
