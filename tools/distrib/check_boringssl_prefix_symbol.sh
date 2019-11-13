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


# Check if the current BoringSSL version's corresponding prefix header is uploaded to GCS.
set -e

cd "$(dirname $0)"
cd ../../third_party/boringssl

BORINGSSL_COMMIT=$(git rev-parse HEAD)

curl -f -L https://storage.googleapis.com/grpc_boringssl_prefix_headers/boringssl_prefix_symbols-$BORINGSSL_COMMIT.h > /dev/null

[ $? == 0 ] || { echo "Cannot find prefix header of current BoringSSL commit ($BORINGSSL_COMMIT) on GCS." ; echo "Generate with tools/distrib/upgrade_boringssl_objc.sh" ; exit 1 ; }

exit 0
