#!/bin/bash
# Copyright 2019 gRPC authors.
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

[ $# == 1 ] || { echo "Usage: generate_boringssl_prefix_header.sh <boringssl_commit>" ; exit 1 ; }

git clone -n https://github.com/google/boringssl.git
cd boringssl
git checkout $1 || { echo "Unable to checkout the commit $1" ; exit 1 ; }
mkdir build
cd build
cmake ..

# gcc crashes on docker when using -j with too many cores. Limiting to 4 seems to be fine.
make -j4

[ -f ssl/libssl.a ] || { echo "Failed to build libssl.a" ; exit 1 ; }
[ -f crypto/libcrypto.a ] || { echo "Failed to build libcrypto.a" ; exit 1 ; }

go run ../util/read_symbols.go ssl/libssl.a > ./symbols.txt
go run ../util/read_symbols.go crypto/libcrypto.a >> ./symbols.txt

cmake .. -DBORINGSSL_PREFIX=GRPC -DBORINGSSL_PREFIX_SYMBOLS=symbols.txt
make boringssl_prefix_symbols

[ -f symbol_prefix_include/boringssl_prefix_symbols.h ] || { echo "Failed to build boringssl_prefix_symbols.sh" ; exit 1 ; }

gzip -c symbol_prefix_include/boringssl_prefix_symbols.h | base64 > /output/boringssl_prefix_symbols.h.gz.b64

exit 0
