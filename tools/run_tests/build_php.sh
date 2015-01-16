#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

export GRPC_DIR=`pwd`

# make the libraries
make -j shared_c

# build php
cd src/php

cd ext/grpc
phpize
cd ../..
ext/grpc/configure
#cd ext/grpc
make

