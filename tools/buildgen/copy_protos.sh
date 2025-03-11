#! /bin/bash
# Copyright 2025 gRPC authors.
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
# Copy protos from proto directory to python ancillary packages directory.

set -euo pipefail

GRPC_ROOT=$(realpath "$(dirname "$0")/../..")

# Source and destination directories (base paths).
SRC_BASE="$GRPC_ROOT/src/proto/grpc"
DST_BASE="$GRPC_ROOT/src/python"

copy_proto() {
  local PROTO_NAME="$1"
  local SRC_SUBDIR="$2"
  local DST_SUBDIR="$3"

  local SRC_FILE="$SRC_BASE/$SRC_SUBDIR/$PROTO_NAME.proto"
  local DST_FILE="$DST_BASE/$DST_SUBDIR/$proto_name.proto"

  cp "$SRC_FILE" "$DST_FILE"
  echo "Copied: $SRC_FILE -> $DST_FILE"
}

copy_proto "channelz" "channelz" "grpcio_channelz/grpc_channelz/v1"
copy_proto "health" "health/v1" "grpcio_health_checking/grpc_health/v1"
copy_proto "reflection" "reflection/v1alpha" "grpcio_reflection/grpc_reflection/v1alpha"

echo "Proto files copied successfully."
