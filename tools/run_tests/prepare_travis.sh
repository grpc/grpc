#!/bin/bash

cd `dirname $0`/../..
grpc_dir=`pwd`

distrib=`md5sum /etc/issue | cut -f1 -d\ `
echo "Configuring for disbribution $distrib"
git submodule | while read sha path extra ; do
  cd /tmp
  name=`basename $path`
  file=$name-$sha-$CONFIG-prebuilt-$distrib.tar.gz
  echo -n "$file ..."
  url=http://storage.googleapis.com/grpc-prebuilt-packages/$file
  wget -q $url && (
    echo " Found."
    tar xfz $file
  ) || true
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
