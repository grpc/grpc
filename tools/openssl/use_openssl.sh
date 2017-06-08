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

set -ex

cd $(dirname $0)/../..
set root=`pwd`
CC=${CC:-cc}

# allow openssl to be pre-downloaded
if [ ! -e third_party/openssl-1.0.2f.tar.gz ]
then
  echo "Downloading https://openssl.org/source/old/1.0.2/openssl-1.0.2f.tar.gz to third_party/openssl-1.0.2f.tar.gz"
  wget https://openssl.org/source/old/1.0.2/openssl-1.0.2f.tar.gz -O third_party/openssl-1.0.2f.tar.gz
fi

# clean openssl directory
rm -rf third_party/openssl-1.0.2f

# extract archive
cd third_party
tar xfz openssl-1.0.2f.tar.gz

# build openssl
cd openssl-1.0.2f
CC="$CC -fPIC -fvisibility=hidden" ./config no-asm
make

# generate the 'grpc_obj' directory needed by the makefile
mkdir grpc_obj
cd grpc_obj
ar x ../libcrypto.a
ar x ../libssl.a
