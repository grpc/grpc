#!/bin/bash -ex

#install ubuntu pre-requisites
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python python-pip clang
sudo pip install six

# set up ebs volume for build 
sudo mkdir /mnt/build
sudo mount -t xfs /dev/nvme1n1 /mnt/build
sudo chown ubuntu /mnt/build

mv grpc /mnt/build
cd /mnt/build/grpc
# build with bazel
export CXX=clang++
export CC=clang
tools/bazel test --config=dbg //test/...
results=$?
sudo shutdown
exit $results

