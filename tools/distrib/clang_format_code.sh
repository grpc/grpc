#!/bin/bash

set -ex

# change to root directory
cd $(dirname $0)/../..

# build clang-format docker image
docker build -t grpc_clang_format tools/dockerfile/grpc_clang_format

# run clang-format against the checked out codebase
docker run -e TEST=$TEST --rm=true -v `pwd`:/local-code -t grpc_clang_format /clang_format_all_the_things.sh

