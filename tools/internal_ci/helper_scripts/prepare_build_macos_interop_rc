#!/bin/bash
# Copyright 2017 gRPC authors.
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
# Source this rc script to prepare the environment for MacOS interop
# builds. This rc script must be used in the root directory of gRPC
# and is expected to be used before prepare_build_macos_rc

# Clone repositories for languages tested by the interop test suite
git clone --recursive https://github.com/grpc/grpc-go ./../grpc-go && (cd ../grpc-go; git rev-parse HEAD)
git clone --recursive https://github.com/grpc/grpc-java ./../grpc-java && (cd ../grpc-java; git rev-parse HEAD)
git clone --recursive https://github.com/grpc/grpc-node ./../grpc-node && (cd ../grpc-node; git rev-parse HEAD)
git clone --recursive https://github.com/grpc/grpc-dart ./../grpc-dart && (cd ../grpc-dart; git rev-parse HEAD)
git clone --recursive https://github.com/grpc/grpc-dotnet ./../grpc-dotnet && (cd ../grpc-dotnet; git rev-parse HEAD)
