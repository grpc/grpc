#!/bin/bash
# Copyright 2015, Google Inc.
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

source ~/.rvm/scripts/rvm
set -ex

cd $(dirname $0)/../../..

CONFIG=${CONFIG:-opt}

# build C++ qps worker & driver always - we need at least the driver to
# run any of the scenarios.
# TODO(jtattermusch): not embedding OpenSSL breaks the C# build because
# grpc_csharp_ext needs OpenSSL embedded and some intermediate files from
# this build will be reused.
make CONFIG=${CONFIG} EMBED_OPENSSL=true EMBED_ZLIB=true qps_worker qps_json_driver -j8

for language in $@
do
  case "$language" in
  "c++")
    ;;  # C++ has already been built.
  "java")
    (cd ../grpc-java/ &&
      ./gradlew -PskipCodegen=true :grpc-benchmarks:installDist)
    ;;
  "go")
    tools/run_tests/performance/build_performance_go.sh
    ;;
  *)
    tools/run_tests/run_tests.py -l $language -c $CONFIG --build_only -j 8
    ;;
  esac
done
