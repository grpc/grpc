#!/bin/bash
# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

git clone $EXTERNAL_GIT_ROOT
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd ${EXTERNAL_GIT_ROOT} && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference ${EXTERNAL_GIT_ROOT}/${name} \
${name}')

cd grpc

cd third_party/protobuf && ./autogen.sh && \
./configure && make -j4 && make check && make install && ldconfig

cd ../.. && make -j4 && make install

cd examples/cpp/helloworld

make

make clean

cd ../../../examples/cpp/route_guide

make

make clean

