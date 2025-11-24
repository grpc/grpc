#!/bin/bash
# Copyright 2022 The gRPC Authors
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

echo -e "System: $(uname -a)\n"
echo -e "Hostname: $(hostname)\n"
echo -e "Cmake: $(cmake --version)\n"
echo -e "-- OS --\n$(lsb_release -a 2>/dev/null)\n"
echo -e "-- CPU --\n$(lscpu)\n"
echo -e "-- Memory --\n$(lsmem --summary)\n$(free -h --si)\n"
echo -e "-- Block devices --\n$(lsblk --all --fs --paths)\n"

set -ex

# Prepend verbose mode commands (xtrace) with the date.
PS4='+ $(date "+[%H:%M:%S %Z]")\011 '
echo "sergiitk@ >> Started build_cxx.sh"

# Set install path to avoid installing to system paths
cd "$(dirname "$0")/../../.."
mkdir -p cmake/install
INSTALL_PATH="$(pwd)/cmake/install"

echo 'sergiitk@ >> Install abseil-cpp since opentelemetry CMake uses find_package to find it.'
cd third_party/abseil-cpp
mkdir build
cd build
cmake -DABSL_BUILD_TESTING=OFF -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ..
make -j"${GRPC_RUN_TESTS_JOBS}" install

echo 'sergiitk@ >> Install opentelemetry-cpp since we only support "package" mode for opentelemetry at present.'
cd ../../..
cd third_party/opentelemetry-cpp
mkdir build
cd build
cmake -DWITH_ABSEIL=ON -DBUILD_TESTING=OFF -DWITH_BENCHMARK=OFF -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ..
make -j"${GRPC_RUN_TESTS_JOBS}" install

cd ../../..
mkdir -p cmake/build
cd cmake/build

echo "sergiitk@ >> Prepping for the main build"
# TODO(yashykt/veblush): Remove workaround after fixing b/332425004
if [ "${GRPC_RUNTESTS_ARCHITECTURE}" = "x86" ]; then
    cmake -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" "$@" ../..
else
    cmake -DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=ON -DgRPC_ABSL_PROVIDER=package -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ../..
fi

if [[ "$*" =~ "-DgRPC_BUILD_TESTS=OFF" ]]; then
    echo "sergiitk@ >> Just build grpc++ target when gRPC_BUILD_TESTS is OFF (This is a temporary mitigation for gcc 7. Remove this once gcc 7 is removed from the supported compilers)"
    make -j"${GRPC_RUN_TESTS_JOBS}" "grpc++"
else
    # GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX will be set to either "c" or "cxx"
    echo "sergiitk@ >> Build with tests"

    echo "sergiitk@ >> Running buildtests_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}"
    make -j"${GRPC_RUN_TESTS_JOBS}" "buildtests_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}"

    echo "sergiitk@ >> Running tools_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}"
    make -j"${GRPC_RUN_TESTS_JOBS}" "tools_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}"
fi

echo "Finished build_cxx.sh"
