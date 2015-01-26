#!/bin/bash

set -ex

CONFIG=${CONFIG:-opt}

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
export GRPC_LIB_SUBDIR=libs/$CONFIG

# build php
cd src/php

cd ext/grpc
phpize
./configure --enable-grpc=$root
make
