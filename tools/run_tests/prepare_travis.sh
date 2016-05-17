#!/bin/bash
# Copyright 2015, Google Inc.
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

cd `dirname $0`/../..
grpc_dir=`pwd`

distrib=`md5sum /etc/issue | cut -f1 -d\ `
echo "Configuring for distribution $distrib"
git submodule | while read sha path extra ; do
  cd /tmp
  name=`basename $path`
  file=$name-$sha-$CONFIG-prebuilt-$distrib.tar.gz
  echo -n "Looking for $file ..."
  url=http://storage.googleapis.com/grpc-prebuilt-packages/$file
  wget -q $url && (
    echo " Found."
    tar xfz $file
  ) || echo " Not found."
done

mkdir -p bins/$CONFIG/protobuf
mkdir -p libs/$CONFIG/protobuf
mkdir -p libs/$CONFIG/openssl

function cpt {
  cp /tmp/prebuilt/$1 $2/$CONFIG/$3
  touch $2/$CONFIG/$3/`basename $1`
}

if [ -e /tmp/prebuilt/bin/protoc ] ; then
  touch third_party/protobuf/configure
  cpt bin/protoc bins protobuf
  cpt lib/libprotoc.a libs protobuf
  cpt lib/libprotobuf.a libs protobuf
fi

if [ -e /tmp/prebuilt/lib/libssl.a ] ; then
  cpt lib/libcrypto.a libs openssl
  cpt lib/libssl.a libs openssl
fi
