#!/bin/bash
# Copyright 2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

dotnet restore Grpc.sln

# To be able to build, we also need to put grpc_csharp_ext to its normal location
mkdir -p ../../libs/opt
cp nativelibs/linux_x64/libgrpc_csharp_ext.so ../../libs/opt

dotnet pack --configuration Release Grpc.Core --output ../../../artifacts
dotnet pack --configuration Release Grpc.Core.Testing --output ../../../artifacts
dotnet pack --configuration Release Grpc.Auth --output ../../../artifacts
dotnet pack --configuration Release Grpc.HealthCheck --output ../../../artifacts
dotnet pack --configuration Release Grpc.Reflection --output ../../../artifacts

nuget pack Grpc.nuspec -Version "1.3.0" -OutputDirectory ../../artifacts
nuget pack Grpc.Tools.nuspec -Version "1.3.0" -OutputDirectory ../../artifacts

(cd ../../artifacts && zip csharp_nugets_dotnetcli.zip *.nupkg)
