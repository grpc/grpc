#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`

# make the libraries
make -j static_c

# build php
cd src/php

cd ext/grpc
phpize
./configure --enable-grpc=$root
make

