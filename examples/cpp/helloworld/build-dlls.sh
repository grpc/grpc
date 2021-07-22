#!/bin/sh

rm -rf cmake/build
mkdir -p cmake/build
pushd cmake/build
cmake -G "Visual Studio 16 2019" -A x64 ../.. -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE
popd

