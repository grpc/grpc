#!/bin/sh
rm -rf cmake/build
mkdir -p cmake/build
pushd cmake/build

# See https://github.com/protocolbuffers/protobuf/blob/master/cmake/README.md#dlls-vs-static-linking
cmake -G "Visual Studio 16 2019" -A x64 ../.. -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE -Dprotobuf_BUILD_SHARED_LIBS=ON
popd

MSBUILD_EXECUTABLE="MSBuild.exe"

MSBuild.exe cmake/build/ALL_BUILD.vcxproj
