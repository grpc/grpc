#!/bin/bash
# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -eo pipefail

cd `dirname $0`

BINDIR=`pwd`/../../../bins/$CONFIG
TMP_PATH=$PATH

# If `protoc` is not found, add bins/$CONFIG/protobuf/protoc to the search path
hash protoc 2>/dev/null || TMP_PATH=$BINDIR/protobuf:$TMP_PATH

# If `protoc-gen-objcgrpc` is not found, make a symlink from
# bins/$CONGIF/grpc_objective_c_plugin and add it to the search path
PATH=$TMP_PATH hash protoc-gen-objcgrpc 2>/dev/null || {
  ln -sf $BINDIR/grpc_objective_c_plugin $BINDIR/protoc-gen-objcgrpc
  TMP_PATH=$BINDIR:$TMP_PATH
}

SCHEME=HelloWorld                              \
  EXAMPLE_PATH=examples/objective-c/helloworld \
  PATH=$TMP_PATH                               \
  ./build_one_example.sh

SCHEME=RouteGuideClient                         \
  EXAMPLE_PATH=examples/objective-c/route_guide \
  PATH=$TMP_PATH                                \
  ./build_one_example.sh

SCHEME=AuthSample                               \
  EXAMPLE_PATH=examples/objective-c/auth_sample \
  PATH=$TMP_PATH                                \
  ./build_one_example.sh

SCHEME=Sample                                  \
  EXAMPLE_PATH=src/objective-c/examples/Sample \
  PATH=$TMP_PATH                               \
  ./build_one_example.sh

SCHEME=SwiftSample                                  \
  EXAMPLE_PATH=src/objective-c/examples/SwiftSample \
  PATH=$TMP_PATH                                    \
  ./build_one_example.sh
