#!/bin/bash
# Copyright 2018 gRPC authors.
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

# Check if the current BoringSSL prefix symbols is up to date
set -e

cd "$(dirname $0)"
cd ../../third_party/boringssl

BORINGSSL_COMMIT=$(git rev-parse HEAD)

mkdir -p ./build
cd build
cmake ..
make -j4
go run ../util/read_symbols.go ssl/libssl.a > ./symbols.txt
go run ../util/read_symbols.go crypto/libcrypto.a >> ./symbols.txt

cmake .. -DBORINGSSL_PREFIX=GRPC -DBORINGSSL_PREFIX_SYMBOLS=symbols.txt
make boringssl_prefix_symbols
gzip -c symbol_prefix_include/boringssl_prefix_symbols.h | base64 > boringssl_prefix_symbols.h.gz.b64

diff ../../../src/boringssl/boringssl_prefix_symbols.h.gz.b64 boringssl_prefix_symbols.h.gz.b64

result=$?

exit $result
