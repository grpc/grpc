#!/bin/bash

set -ex

CONFIG=${CONFIG:-opt}

# change to grpc repo root
cd $(dirname $0)/../..

# tells npm install to look for files in that directory
export GRPC_ROOT=`pwd`
# tells npm install the subdirectory with library files
export GRPC_LIB_SUBDIR=libs/$CONFIG
# tells npm install not to use default locations
export GRPC_NO_INSTALL=yes

cd src/node

npm install
