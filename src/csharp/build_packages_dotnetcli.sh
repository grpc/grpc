#!/bin/bash
# Copyright 2016 gRPC authors.
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

cd $(dirname $0)

mkdir -p ../../artifacts/

# Collect the artifacts built by the previous build step
mkdir -p nativelibs
# Jenkins flow (deprecated)
cp -r $EXTERNAL_GIT_ROOT/platform={windows,linux,macos}/artifacts/csharp_ext_* nativelibs || true
# Kokoro flow
cp -r $EXTERNAL_GIT_ROOT/input_artifacts/csharp_ext_* nativelibs || true

# Collect protoc artifacts built by the previous build step
mkdir -p protoc_plugins
# Jenkins flow (deprecated)
cp -r $EXTERNAL_GIT_ROOT/platform={windows,linux,macos}/artifacts/protoc_* protoc_plugins || true
# Kokoro flow
cp -r $EXTERNAL_GIT_ROOT/input_artifacts/protoc_* protoc_plugins || true
find protoc_plugins/ -type f -name protoc -exec chmod +x {} ";"
find protoc_plugins/ -type f -name grpc_csharp_plugin -exec chmod +x {} ";"

dotnet restore Grpc.sln

# To be able to build, we also need to put grpc_csharp_ext to its normal location
mkdir -p ../../libs/opt
cp nativelibs/csharp_ext_linux_x64/libgrpc_csharp_ext.so ../../libs/opt

dotnet pack --configuration Release Grpc.Core --output ../../../artifacts
dotnet pack --configuration Release Grpc.Core.Testing --output ../../../artifacts
dotnet pack --configuration Release Grpc.Auth --output ../../../artifacts
dotnet pack --configuration Release Grpc.HealthCheck --output ../../../artifacts
dotnet pack --configuration Release Grpc.Reflection --output ../../../artifacts

nuget pack Grpc.nuspec -Version "1.11.0-pre1" -OutputDirectory ../../artifacts
nuget pack Grpc.Tools.nuspec -Version "1.11.0-pre1" -OutputDirectory ../../artifacts

(cd ../../artifacts && zip csharp_nugets_dotnetcli.zip *.nupkg)
