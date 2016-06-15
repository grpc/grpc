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

PROTO_DIR="src/proto/grpc/reflection/v1alpha"
PROTO_FILE="reflection"
HEADER_DIR="include/grpc++/ext"
SRC_DIR="src/cpp/ext"
INCLUDE_DIR="grpc++/ext"
TMP_DIR="tmp"
GRPC_PLUGIN="bins/opt/grpc_cpp_plugin"
PROTOC=third_party/protobuf/src/protoc

set -e

TMP_DIR=${TMP_DIR}_${PROTO_FILE}

cd $(dirname $0)/../../..

[ ! -d $HEADER_DIR ] && mkdir -p $HEADER_DIR || :
[ ! -d $SRC_DIR ] && mkdir -p $SRC_DIR || :
[ ! -d $TMP_DIR ] && mkdir -p $TMP_DIR || :

$PROTOC -I$PROTO_DIR --cpp_out=$TMP_DIR ${PROTO_DIR}/${PROTO_FILE}.proto
$PROTOC -I$PROTO_DIR --grpc_out=$TMP_DIR --plugin=protoc-gen-grpc=${GRPC_PLUGIN} ${PROTO_DIR}/${PROTO_FILE}.proto

sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.pb.cc
sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.h
sed -i "s/\"${PROTO_FILE}.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc
sed -i "s/\"${PROTO_FILE}.grpc.pb.h\"/<${INCLUDE_DIR/\//\\\/}\/${PROTO_FILE}.grpc.pb.h>/g" ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc

/bin/cp LICENSE ${TMP_DIR}/TMP_LICENSE
sed -i -e "s/./ &/" -e "s/.*/ \*&/" ${TMP_DIR}/TMP_LICENSE
sed -i -r "\$a\ *\n *\/\n\n"  ${TMP_DIR}/TMP_LICENSE

sed -i -e "1s/^/ *\n/" -e "1s/^/\/*\n/" ${TMP_DIR}/*.pb.h
sed -i -e "1s/^/ *\n/" -e "1s/^/\/*\n/" ${TMP_DIR}/*.pb.cc

sed -i "2r ${TMP_DIR}/TMP_LICENSE" ${TMP_DIR}/*.pb.h
sed -i "2r ${TMP_DIR}/TMP_LICENSE" ${TMP_DIR}/*.pb.cc

/bin/mv ${TMP_DIR}/${PROTO_FILE}.pb.h ${HEADER_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.grpc.pb.h ${HEADER_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.pb.cc ${SRC_DIR}
/bin/mv ${TMP_DIR}/${PROTO_FILE}.grpc.pb.cc ${SRC_DIR}
/bin/rm -r $TMP_DIR
