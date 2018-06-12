#!/bin/sh
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

# Builds an experimental .unitypackage file to be imported into Unity projects.

set -ex

cd "$(dirname "$0")/.."

dotnet restore Grpc.sln

mkdir -p GrpcUnity
dotnet build --configuration Release --framework net45 Grpc.Core --output ../GrpcUnity

#TODO: add ThirdParty/Grpc.Core:
# - assembly
# - native libraries (mac dylib need to be renamed to grpc_csharp_ext.bundle)

#TODO: add ThirdParty/Grpc.Tools:
# - protoc and grpc plugin
