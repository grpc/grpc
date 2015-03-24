#!/bin/sh

cd `dirname $0`/../..
grpc_dir=`pwd`

distrib=`md5sum /etc/issue | cut -f1 -d\ `
git submodule | while read sha path extra ; do
  cd /tmp
  name=`basename $path`
  file=$name-$sha-$CONFIG-prebuilt-$distrib.tar.gz
  echo $file
  url=http://storage.googleapis.com/grpc-prebuilt-packages/$file
  wget -q $url && tar xfz $file || true
done
