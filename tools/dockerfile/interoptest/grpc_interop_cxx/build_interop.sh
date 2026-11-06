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
# Builds C++ interop server and client in a base image.
set -e

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc

# Install the roots.pem
mkdir -p /usr/local/share/grpc
cp etc/roots.pem /usr/local/share/grpc/roots.pem

# Build C++ interop client, server, http2 client, and OTLP collector using Bazel.
# Bazel is required because OpenTelemetry C++ dependencies and otlp_collector
# are managed hermetically via Bazel/Bzlmod, whereas CMake does not vendor OTel.
tools/bazel build //test/cpp/interop:interop_client //test/cpp/interop:interop_server //test/cpp/interop:http2_client //test/cpp/interop:otlp_collector

# Place binaries in cmake/build/ to preserve the path contract expected by
# run_interop_tests.py (CXXLanguage) and historical interop test harnesses.
mkdir -p cmake/build
cp -f bazel-bin/test/cpp/interop/interop_client cmake/build/interop_client
cp -f bazel-bin/test/cpp/interop/interop_server cmake/build/interop_server
cp -f bazel-bin/test/cpp/interop/http2_client cmake/build/http2_client

