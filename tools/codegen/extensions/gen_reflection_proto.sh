#!/bin/bash
PROTO_DIR="src/proto/grpc/reflection/v1alpha"
PROTO_FILE="reflection"
HEADER_DIR="extensions/include/grpc++/impl"
SRC_DIR="extensions/reflection"
INCLUDE_DIR="grpc++/impl"
TMP_DIR="tmp"
GRPC_PLUGIN="bins/opt/grpc_cpp_plugin"
PROTOC=protoc

set -e

TMP_DIR=${TMP_DIR}_${PROTO_FILE}

cd $(dirname $0)/../../..

[ ! -d $HEADER_DIR ] && mkdir -p $HEADER_DIR || :
[ ! -d $SRC_DIR ] && mkdir -p $SRC_DIR || :
[ ! -d $TMP_DIR ] && mkdir -p $TMP_DIR || :

$PROTOC -I$PROTO_DIR --cpp_out=$TMP_DIR ${PROTO_DIR}/${PROTO_FILE}.proto
$PROTOC -I$PROTO_DIR --grpc_out=$TMP_DIR --plugin=protoc-gen-grpc=${GRPC_PLUGIN} ${PROTO_DIR}/${PROTO_FILE}.proto

sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.pb.cc
sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc
sed -i "s/\"${PROTO_FILE}.grpc.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.grpc.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc
sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.h

/bin/mv ${TMP_DIR}/${PROTO_FILE}.pb.h ${HEADER_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.grpc.pb.h ${HEADER_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.pb.cc ${SRC_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc ${SRC_DIR}
/bin/rm -r $TMP_DIR
