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

BORINGSSL_COMMIT=$(cd ../../third_party/boringssl ; git rev-parse HEAD)

mkdir -p ./output

docker build ../dockerfile/grpc_objc/generate_boringssl_prefix_header -t grpc/boringssl_prefix_header
docker run -it --rm -v $(pwd)/output:/output grpc/boringssl_prefix_header $BORINGSSL_COMMIT

diff ../../src/boringssl/boringssl_prefix_symbols.h.gz.b64 output/boringssl_prefix_symbols.h.gz.b64

result=$?

rm -rf ./output

exit $result
