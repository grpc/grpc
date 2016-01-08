#!/bin/bash
#
# Example usage:
#   tools/codegen/core/gen_load_balancing_proto.sh \
#     src/proto/grpc/lb/v0/load_balancer.proto

if [ $# -eq 0 ]; then
  echo "Usage: $0 <load_balancer.proto>"
  exit 1
fi

readonly EXPECTED_OPTIONS_FILE_PATH="${1%.*}.options"

if [[ ! -f "$1" ]]; then
  echo "Input proto file '$1' doesn't exist."
  exit 2
fi
if [[ ! -f "${EXPECTED_OPTIONS_FILE_PATH}" ]]; then
  echo "Expected nanopb options file '${EXPECTED_OPTIONS_FILE_PATH}' missing"
  exit 3
fi

readonly GRPC_ROOT=$PWD

pushd "$(dirname $1)" > /dev/null

protoc \
--plugin=protoc-gen-nanopb="$GRPC_ROOT/third_party/nanopb/generator/protoc-gen-nanopb" \
--nanopb_out='-L#include\ \"third_party/nanopb/pb.h\"'":$GRPC_ROOT/src/core/proto/grpc/lb/v0" \
"$(basename $1)"

readonly PROTO_BASENAME=$(basename $1 .proto)
sed -i "s:$PROTO_BASENAME.pb.h:src/core/proto/grpc/lb/v0/$PROTO_BASENAME.pb.h:g" \
    "$GRPC_ROOT/src/core/proto/grpc/lb/v0/$PROTO_BASENAME.pb.c"
clang-format -i "$GRPC_ROOT/src/core/proto/grpc/lb/v0/$PROTO_BASENAME.pb.c"
clang-format -i "$GRPC_ROOT/src/core/proto/grpc/lb/v0/$PROTO_BASENAME.pb.h"

popd > /dev/null
