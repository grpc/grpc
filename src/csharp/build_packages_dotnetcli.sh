#!/bin/bash
# Copyright 2016, gRPC authors
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

set -ex

cd $(dirname $0)

mkdir -p ../../artifacts/

mkdir -p nativelibs/windows_x86 nativelibs/windows_x64 \
    nativelibs/linux_x86 nativelibs/linux_x64 \
    nativelibs/macosx_x86 nativelibs/macosx_x64

mkdir -p protoc_plugins/windows_x86 protoc_plugins/windows_x64 \
    protoc_plugins/linux_x86 protoc_plugins/linux_x64 \
    protoc_plugins/macosx_x86 protoc_plugins/macosx_x64

# Collect the artifacts built by the previous build step if running on Jenkins
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=csharp,platform=windows/artifacts/* nativelibs/windows_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=csharp,platform=windows/artifacts/* nativelibs/windows_x64 || true
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=csharp,platform=linux/artifacts/* nativelibs/linux_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=csharp,platform=linux/artifacts/* nativelibs/linux_x64 || true
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=csharp,platform=macos/artifacts/* nativelibs/macosx_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=csharp,platform=macos/artifacts/* nativelibs/macosx_x64 || true

# Collect protoc artifacts built by the previous build step
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=protoc,platform=windows/artifacts/* protoc_plugins/windows_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=protoc,platform=windows/artifacts/* protoc_plugins/windows_x64 || true
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=protoc,platform=linux/artifacts/* protoc_plugins/linux_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=protoc,platform=linux/artifacts/* protoc_plugins/linux_x64 || true
cp $EXTERNAL_GIT_ROOT/architecture=x86,language=protoc,platform=macos/artifacts/* protoc_plugins/macosx_x86 || true
cp $EXTERNAL_GIT_ROOT/architecture=x64,language=protoc,platform=macos/artifacts/* protoc_plugins/macosx_x64 || true

dotnet restore .

dotnet pack --configuration Release Grpc.Core/project.json --output ../../artifacts
dotnet pack --configuration Release Grpc.Core.Testing/project.json --output ../../artifacts
dotnet pack --configuration Release Grpc.Auth/project.json --output ../../artifacts
dotnet pack --configuration Release Grpc.HealthCheck/project.json --output ../../artifacts
dotnet pack --configuration Release Grpc.Reflection/project.json --output ../../artifacts

nuget pack Grpc.nuspec -Version "1.2.0-dev" -OutputDirectory ../../artifacts
nuget pack Grpc.Tools.nuspec -Version "1.2.0-dev" -OutputDirectory ../../artifacts

(cd ../../artifacts && zip csharp_nugets_dotnetcli.zip *.nupkg)
