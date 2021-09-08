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
cd ../../third_party/boringssl-with-bazel

BORINGSSL_COMMIT=$(git rev-parse HEAD)
PREFIX_SYMBOLS_COMMIT=$(cat ../../src/boringssl/boringssl_prefix_symbols.h | head -n1 | awk '{print $NF}')

[ $BORINGSSL_COMMIT == $PREFIX_SYMBOLS_COMMIT ] || { echo "The BoringSSL commit does not match the commit of the prefix symbols (src/boringssl/boringssl_prefix_symbols.h). Run tools/distrib/generate_boringssl_prefix_header.sh to update the prefix symbols." ; exit 1 ; }

exit 0
