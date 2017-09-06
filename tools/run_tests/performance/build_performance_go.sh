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

cd $(dirname $0)/../../..

export GOPATH=$(pwd)/../gopath

# Get grpc-go and the dependencies but get rid of the upstream/master version
go get google.golang.org/grpc
rm -rf "${GOPATH}/src/google.golang.org/grpc"

# Get the revision of grpc-go we want to test
git clone --recursive ../grpc-go ${GOPATH}/src/google.golang.org/grpc

(cd ${GOPATH}/src/google.golang.org/grpc/benchmark/worker && go install)
