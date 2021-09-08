#!/bin/bash
# Copyright 2020 The gRPC Authors
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

cd "$(dirname "$0")"

mkdir -p ../../artifacts

# Collect the artifacts built by the previous build step
mkdir -p nativelibs
cp -r "${EXTERNAL_GIT_ROOT}"/input_artifacts/csharp_ext_* nativelibs || true

# Add current timestamp to dev nugets
./expand_dev_version.sh

# Extract current Grpc.Core version from build/dependencies.props
UNITYPACKAGE_VERSION="$(grep -o '<GrpcCsharpVersion>.*</GrpcCsharpVersion>' build/dependencies.props | sed 's/<GrpcCsharpVersion>//' | sed 's/<\/GrpcCsharpVersion>//')"

dotnet restore Grpc.sln

# To be able to build the Grpc.Core project, we also need to put grpc_csharp_ext to where Grpc.Core.csproj
# expects it.
mkdir -p ../../cmake/build
cp nativelibs/csharp_ext_linux_x64/libgrpc_csharp_ext.so ../../cmake/build

dotnet build --configuration Release Grpc.Core
# build HealthCheck to get hold of Google.Protobuf.dll assembly
dotnet build --configuration Release Grpc.HealthCheck

# copy Grpc assemblies to the unity package skeleton
# TODO(jtattermusch): Add Grpc.Auth assembly and its dependencies
cp Grpc.Core.Api/bin/Release/net45/Grpc.Core.Api.dll unitypackage/unitypackage_skeleton/Plugins/Grpc.Core.Api/lib/net45/Grpc.Core.Api.dll
cp Grpc.Core.Api/bin/Release/net45/Grpc.Core.Api.pdb unitypackage/unitypackage_skeleton/Plugins/Grpc.Core.Api/lib/net45/Grpc.Core.Api.pdb
cp Grpc.Core.Api/bin/Release/net45/Grpc.Core.Api.xml unitypackage/unitypackage_skeleton/Plugins/Grpc.Core.Api/lib/net45/Grpc.Core.Api.xml
cp Grpc.Core/bin/Release/net45/Grpc.Core.dll unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/lib/net45/Grpc.Core.dll
cp Grpc.Core/bin/Release/net45/Grpc.Core.pdb unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/lib/net45/Grpc.Core.pdb
cp Grpc.Core/bin/Release/net45/Grpc.Core.xml unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/lib/net45/Grpc.Core.xml

# copy desktop native libraries to the unity package skeleton
cp nativelibs/csharp_ext_linux_x64/libgrpc_csharp_ext.so unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/linux/x64/libgrpc_csharp_ext.so
cp nativelibs/csharp_ext_macos_x64/libgrpc_csharp_ext.dylib unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/osx/x64/grpc_csharp_ext.bundle
cp nativelibs/csharp_ext_windows_x86/grpc_csharp_ext.dll unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/win/x86/grpc_csharp_ext.dll
cp nativelibs/csharp_ext_windows_x64/grpc_csharp_ext.dll unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/win/x64/grpc_csharp_ext.dll

# add Android and iOS native libraries
cp nativelibs/csharp_ext_linux_android_armeabi-v7a/libgrpc_csharp_ext.so unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/android/armeabi-v7a/libgrpc_csharp_ext.so
cp nativelibs/csharp_ext_linux_android_arm64-v8a/libgrpc_csharp_ext.so unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/android/arm64-v8a/libgrpc_csharp_ext.so
cp nativelibs/csharp_ext_linux_android_x86/libgrpc_csharp_ext.so unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/android/x86/libgrpc_csharp_ext.so
cp nativelibs/csharp_ext_macos_ios/libgrpc_csharp_ext.a unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/ios/libgrpc_csharp_ext.a
cp nativelibs/csharp_ext_macos_ios/libgrpc.a unitypackage/unitypackage_skeleton/Plugins/Grpc.Core/runtimes/ios/libgrpc.a

# add gRPC dependencies
# TODO(jtattermusch): also include XMLdoc
cp Grpc.Core/bin/Release/net45/System.Runtime.CompilerServices.Unsafe.dll unitypackage/unitypackage_skeleton/Plugins/System.Runtime.CompilerServices.Unsafe/lib/net45/System.Runtime.CompilerServices.Unsafe.dll
cp Grpc.Core/bin/Release/net45/System.Buffers.dll unitypackage/unitypackage_skeleton/Plugins/System.Buffers/lib/net45/System.Buffers.dll
cp Grpc.Core/bin/Release/net45/System.Memory.dll unitypackage/unitypackage_skeleton/Plugins/System.Memory/lib/net45/System.Memory.dll

# add Google.Protobuf
# TODO(jtattermusch): also include XMLdoc
cp Grpc.HealthCheck/bin/Release/net45/Google.Protobuf.dll unitypackage/unitypackage_skeleton/Plugins/Google.Protobuf/lib/net45/Google.Protobuf.dll

# create a zipfile that will act as a Unity package
pushd unitypackage/unitypackage_skeleton
zip -r ../../grpc_unity_package.zip Plugins
popd

cp grpc_unity_package.zip ../../artifacts/grpc_unity_package.${UNITYPACKAGE_VERSION}.zip
