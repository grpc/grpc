#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

# tells npm install to look for files in that directory
export GRPC_ROOT=`pwd`
# tells npm install the subdirectory with library files
export GRPC_LIB_SUBDIR=libs/opt
# tells npm install not to use default locations
export GRPC_NO_INSTALL=yes

# build the c libraries
make -j static_c

cd src/node

npm install
