#!/bin/bash -ex

#install ubuntu pre-requisites
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip clang
sudo pip install six

# setup bazel
BAZEL=bazel-4.0.0-linux-arm64
wget https://github.com/bazelbuild/bazel/releases/download/4.0.0/$BAZEL
wget https://github.com/bazelbuild/bazel/releases/download/4.0.0/${BAZEL}.sha256
chmod +x ${BAZEL}
sha256sum -c ${BAZEL}.sha256

# clone and built repo
cd grpc
# build with bazel
export CXX=clang++
export CC=clang
../${BAZEL} test --config=dbg //test/...

