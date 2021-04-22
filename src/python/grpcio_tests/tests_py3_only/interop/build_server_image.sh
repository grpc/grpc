# Copyright 2021 The gRPC authors.
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
#!/bin/bash

set -x

VERSION=$(git rev-parse HEAD)

PROJECT=grpc-testing
TAG=gcr.io/${PROJECT}/python_xds_interop_server:$VERSION

cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.."

docker build \
    -t ${TAG} \
    -f src/python/grpcio_tests/tests_py3_only/interop/Dockerfile.server \
    .
