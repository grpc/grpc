#!/bin/bash
# Copyright 2015, gRPC authors
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
#
# Builds Go interop server, Stress client and metrics client in a base image.
set -e

# Clone just the grpc-go source code without any dependencies.
# We are cloning from a local git repo that contains the right revision
# to test instead of using "go get" to download from Github directly.
git clone --recursive /var/local/jenkins/grpc-go src/google.golang.org/grpc

# Clone the 'grpc' repo. We just need this for the wrapper scripts under
# grpc/tools/gcp/stress_tests
git clone /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

# Get dependencies from GitHub
# NOTE: once grpc-go dependencies change, this needs to be updated manually
# but we don't expect this to happen any time soon.
go get github.com/golang/protobuf/proto
go get golang.org/x/net/context
go get golang.org/x/net/trace
go get golang.org/x/oauth2
go get google.golang.org/cloud

# Build the interop server, stress client and stress metrics client
(cd src/google.golang.org/grpc/interop/server && go install)
(cd src/google.golang.org/grpc/stress/client && go install)
(cd src/google.golang.org/grpc/stress/metrics_client && go install)
