#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

make -j6

root=`pwd`
virtualenv python2.7_virtual_environment
ln -sf $root/include/grpc python2.7_virtual_environment/include/grpc
source python2.7_virtual_environment/bin/activate
pip install enum34==1.0.4 futures==2.2.0 protobuf==2.6.1
CFLAGS=-I$root/include LDFLAGS=-L$root/libs/opt pip install src/python
