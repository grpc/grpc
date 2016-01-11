#!/bin/bash
#
# Example usage:
#   tools/codegen/core/gen_load_balancing_proto.sh \
#     src/proto/grpc/lb/v0/load_balancer.proto

set -ex
if [ $# -eq 0 ]; then
  echo "Usage: $0 <load_balancer.proto> [output dir]"
  exit 1
fi

readonly GRPC_ROOT=$PWD

OUTPUT_DIR="$GRPC_ROOT/src/core/proto/grpc/lb/v0"
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

pushd "$(dirname $1)" > /dev/null

protoc \
--plugin=protoc-gen-nanopb="$GRPC_ROOT/third_party/nanopb/generator/protoc-gen-nanopb" \
--nanopb_out='-T -L#include\ \"third_party/nanopb/pb.h\"'":$OUTPUT_DIR" \
"$(basename $1)"

readonly PROTO_BASENAME=$(basename $1 .proto)
sed -i "s:$PROTO_BASENAME.pb.h:src/core/proto/grpc/lb/v0/$PROTO_BASENAME.pb.h:g" \
    "$OUTPUT_DIR/$PROTO_BASENAME.pb.c"

clang-format -style="{BasedOnStyle: Google, Language: Cpp}" -i "$OUTPUT_DIR/$PROTO_BASENAME.pb.c"
clang-format -style="{BasedOnStyle: Google, Language: Cpp}" -i "$OUTPUT_DIR/$PROTO_BASENAME.pb.h"

popd > /dev/null
