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

# shellcheck disable=SC1090
source ~/.rvm/scripts/rvm
set -ex

cd "$(dirname "$0")/../../.."
bazel=$(pwd)/tools/bazel

CONFIG=${CONFIG:-opt}

# build C++ qps worker & driver always - we need at least the driver to
# run any of the scenarios.
# TODO(jtattermusch): C++ worker and driver are not buildable on Windows yet
if [ "$OSTYPE" != "msys" ]
then
  # build C++ with cmake as building with "make" disables boringssl assembly
  # optimizations that can have huge impact on secure channel throughput.
  mkdir -p cmake/build
  cd cmake/build
  cmake -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ../..
  make qps_worker qps_json_driver -j8
  cd ../..
  # unbreak subsequent make builds by restoring zconf.h (previously renamed by cmake build)
  # See https://github.com/grpc/grpc/issues/11581
  (cd third_party/zlib; git checkout zconf.h)
fi

PHP_ALREADY_BUILT=""
for language in "$@"
do
  case "$language" in
  "c++")
    ;;  # C++ has already been built.
  "java")
    (cd ../grpc-java/ &&
      ./gradlew -PskipCodegen=true -PskipAndroid=true :grpc-benchmarks:installDist)
    ;;
  "go")
    tools/run_tests/performance/build_performance_go.sh
    ;;
  "php7"|"php7_protobuf_c")
    if [ -n "$PHP_ALREADY_BUILT" ]; then
      echo "Skipping PHP build as already built by $PHP_ALREADY_BUILT"
    else
      PHP_ALREADY_BUILT=$language
      tools/run_tests/performance/build_performance_php7.sh
    fi
    ;;
  "csharp")
    python tools/run_tests/run_tests.py -l "$language" -c "$CONFIG" --build_only -j 8
    # unbreak subsequent make builds by restoring zconf.h (previously renamed by cmake portion of C#'s build)
    # See https://github.com/grpc/grpc/issues/11581
    (cd third_party/zlib; git checkout zconf.h)
    ;;
  "node"|"node_purejs")
    tools/run_tests/performance/build_performance_node.sh
    ;;
  "python")
    $bazel build -c opt //src/python/grpcio_tests/tests/qps:qps_worker
    ;;
  "python_asyncio")
    $bazel build -c opt //src/python/grpcio_tests/tests_aio/benchmark:worker
    ;;
  *)
    python tools/run_tests/run_tests.py -l "$language" -c "$CONFIG" --build_only -j 8
    ;;
  esac
done
