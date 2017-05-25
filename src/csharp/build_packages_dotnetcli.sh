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

# Collect the artifacts built by the previous build step
mkdir -p nativelibs
cp -r $EXTERNAL_GIT_ROOT/platform={windows,linux,macos}/artifacts/csharp_ext_* nativelibs || true

# Collect protoc artifacts built by the previous build step
mkdir -p protoc_plugins
cp -r $EXTERNAL_GIT_ROOT/platform={windows,linux,macos}/artifacts/protoc_* protoc_plugins || true

dotnet restore Grpc.sln

# To be able to build, we also need to put grpc_csharp_ext to its normal location
mkdir -p ../../libs/opt
cp nativelibs/csharp_ext_linux_x64/libgrpc_csharp_ext.so ../../libs/opt

dotnet pack --configuration Release --include-symbols --include-source Grpc.Core --output ../../../artifacts
dotnet pack --configuration Release --include-symbols --include-source Grpc.Core.Testing --output ../../../artifacts
dotnet pack --configuration Release --include-symbols --include-source Grpc.Auth --output ../../../artifacts
dotnet pack --configuration Release --include-symbols --include-source Grpc.HealthCheck --output ../../../artifacts
dotnet pack --configuration Release --include-symbols --include-source Grpc.Reflection --output ../../../artifacts

nuget pack Grpc.nuspec -Version "1.4.0-pre1" -OutputDirectory ../../artifacts
nuget pack Grpc.Tools.nuspec -Version "1.4.0-pre1" -OutputDirectory ../../artifacts

(cd ../../artifacts && zip csharp_nugets_dotnetcli.zip *.nupkg)
