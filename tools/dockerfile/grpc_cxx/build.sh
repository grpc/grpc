#!/bin/bash
rm -rf /var/local/git
cp -R /var/local/git-clone /var/local/git

cd /var/local/git/grpc/third_party/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && make clean

cd /var/local/git/grpc && ls \
  && make clean \
  && make gens/test/cpp/util/messages.pb.cc \
  && make interop_client \
  && make interop_server
