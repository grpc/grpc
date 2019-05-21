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
# Gets a built Go interop server, fake balancer server, and python
# DNS server into a base image.
set -e

# Clone just the grpc-go source code without any dependencies.
# We are cloning from a local git repo that contains the right revision
# to test instead of using "go get" to download from Github directly.
git clone --recursive /var/local/jenkins/grpc-go src/google.golang.org/grpc

# Get all gRPC Go dependencies
(cd src/google.golang.org/grpc && make deps && make testdeps)

# Build the interop server and fake balancer
(cd src/google.golang.org/grpc/interop/server && go install)
(cd src/google.golang.org/grpc/interop/fake_grpclb && go install)
  
# Clone the grpc/grpc repo to get the python DNS server.
# Hack: we don't need to init submodules for the scripts we need.
mkdir -p /var/local/git/grpc
git clone /var/local/jenkins/grpc /var/local/git/grpc
