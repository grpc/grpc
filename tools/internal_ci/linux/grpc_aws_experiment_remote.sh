#!/bin/bash -ex

#install ubuntu pre-requisites
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip
sudo pip install six

# clone and built repo
# rm -rf grpc
# git clone https://github.com/grpc/grpc # 1m
cd grpc
git submodule update --init # 1m
# build with cmake
mkdir -p cmake/build
cd cmake/build
cmake ../..
make

# run tests
cd ../../
python tools/run_tests/run_tests.py -l c++
