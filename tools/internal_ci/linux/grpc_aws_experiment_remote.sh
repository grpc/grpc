#!/bin/bash -ex

#install ubuntu pre-requisites
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip clang
sudo pip install six

cd grpc
# build with bazel
export CXX=clang++
export CC=clang
tools/bazel test --config=dbg //test/...

