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
# Builds Go interop server and client in a base image.
set -e

# Turn on support for Go modules.
export GO111MODULE=on

# Clone just the grpc-go source code without any dependencies.
# We are cloning from a local git repo that contains the right revision
# to test instead of using "go get" to download from Github directly.
git clone --recursive /var/local/jenkins/grpc-go src/google.golang.org/grpc

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

# Build the interop client and server
(cd src/google.golang.org/grpc/interop/client && go install)
(cd src/google.golang.org/grpc/interop/server && go install)
  
