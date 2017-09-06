#!/bin/bash
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

source ~/.rvm/scripts/rvm
set -ex

cd $(dirname $0)/../../..

CONFIG=${CONFIG:-opt}

# build C++ qps worker & driver always - we need at least the driver to
# run any of the scenarios.
# TODO(jtattermusch): C++ worker and driver are not buildable on Windows yet
if [ "$OSTYPE" != "msys" ]
then
  # TODO(jtattermusch): not embedding OpenSSL breaks the C# build because
  # grpc_csharp_ext needs OpenSSL embedded and some intermediate files from
  # this build will be reused.
  make CONFIG=${CONFIG} EMBED_OPENSSL=true EMBED_ZLIB=true qps_worker qps_json_driver -j8
fi

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
  "csharp")
    python tools/run_tests/run_tests.py -l $language -c $CONFIG --build_only -j 8 --compiler coreclr
    ;;
  *)
    python tools/run_tests/run_tests.py -l $language -c $CONFIG --build_only -j 8
    ;;
  esac
done
