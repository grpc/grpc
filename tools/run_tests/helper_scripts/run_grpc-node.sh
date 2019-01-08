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
#
# This script runs grpc/grpc-node tests with their grpc submodule updated
# to this reference

# cd to gRPC root directory
cd "$(dirname "$0")/../../.."

CURRENT_COMMIT="$(git rev-parse --verify HEAD)"

rm -rf ./../grpc-node
git clone --recursive https://github.com/grpc/grpc-node ./../grpc-node
cd ./../grpc-node

./test-grpc-submodule.sh "$CURRENT_COMMIT"
