#! /usr/bin/env bash

set -o errexit
set -o pipefail
set -x

git clone -q --depth=1 https://github.com/tpoechtrager/osxcross.git /opt/osxcross
rm -rf /opt/osxcross/.git

set +x
cd /opt/osxcross/tarballs
set -x
curl -L -o MacOSX11.1.sdk.tar.xz https://github.com/larskanis/MacOSX-SDKs/releases/download/11.1/MacOSX11.1.sdk.tar.xz
tar -xf MacOSX11.1.sdk.tar.xz -C .
cp -rf /usr/lib/llvm-14/include/c++ MacOSX11.1.sdk/usr/include/c++
cp -rf /usr/include/*-linux-gnu/c++/*/bits/ MacOSX11.1.sdk/usr/include/c++/v1/bits
tar -cJf MacOSX11.1.sdk.tar.xz MacOSX11.1.sdk

set +x
cd /opt/osxcross
set -x
UNATTENDED=1 SDK_VERSION=11.1 OSX_VERSION_MIN=10.13 USE_CLANG_AS=1 ./build.sh
ln -s /usr/bin/llvm-config-14 /usr/bin/llvm-config
ENABLE_COMPILER_RT_INSTALL=1 SDK_VERSION=11.1 ./build_compiler_rt.sh
rm -rf *~ build tarballs/*

echo "export PATH=/opt/osxcross/target/bin:\$PATH" >> /etc/rubybashrc
echo "export MACOSX_DEPLOYMENT_TARGET=10.13" >> /etc/rubybashrc
echo "export OSXCROSS_MP_INC=1" >> /etc/rubybashrc
echo "export OSXCROSS_PKG_CONFIG_USE_NATIVE_VARIABLES=1" >> /etc/rubybashrc


# Add links to build tools without target version kind of:
#   arm64-apple-darwin-clang   =>   arm64-apple-darwin20.1-clang
rm -f /opt/osxcross/target/bin/*-apple-darwin-*
find /opt/osxcross/target/bin/ -name '*-apple-darwin[0-9]*' | sort | while read f ; do d=`echo $f | sed s/darwin[0-9\.]*/darwin/`; echo $f '"$@"' | tee $d && chmod +x $d ; done

# There's no objdump in osxcross but we can use llvm's
ln -s /usr/lib/llvm-14/bin/llvm-objdump /opt/osxcross/target/bin/x86_64-apple-darwin-objdump
ln -s /usr/lib/llvm-14/bin/llvm-objdump /opt/osxcross/target/bin/aarch64-apple-darwin-objdump

# install /usr/bin/codesign and make a symlink for codesign_allocate (the architecture doesn't matter)
git clone -q --depth=1 https://github.com/flavorjones/sigtool --branch flavorjones-fix-link-line-library-order
make -C sigtool install
ln -s /opt/osxcross/target/bin/x86_64-apple-darwin[0-9]*-codesign_allocate /usr/bin/codesign_allocate
