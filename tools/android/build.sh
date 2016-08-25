#!/bin/bash

PLATFORM=android-21
ARCH=armeabi

# for gnu libstdc++
#STL=gnu-libstdc++/4.9
#LIB=gnustl_static
#LIBPATH=gnu-libstdc++/4.9/libs/${ARCH}

STL=llvm-libc++/libcxx
LIB=c++_static
LIBPATH=llvm-libc++/libs/${ARCH}

make clean
make HAS_PKG_CONFIG=false \
CC=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-gcc \
CXX=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-g++ \
LD=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-ld \
LDXX=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-ld \
AR="${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-ar crs" \
STRIP=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-strip \
RANLIB=${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-ranlib \
PROTOBUF_CONFIG_OPTS="--host=arm-linux-androideabi" \
PROTOBUF_CPPFLAGS_EXTRA="--sysroot=${ANDROID_NDK}/platforms/${PLATFORM}/arch-arm" \
LDFLAGS="-L${ANDROID_NDK}/platforms/${PLATFORM}/arch-arm/usr/lib \
  -L${ANDROID_NDK}/sources/cxx-stl/${LIBPATH} \
  --sysroot=${ANDROID_NDK}/platforms/${PLATFORM}/arch-arm \
  -l${LIB} -latomic" \
CPPFLAGS="--sysroot=${ANDROID_NDK}/platforms/${PLATFORM}/arch-arm \
  -I${ANDROID_NDK}/platforms/${PLATFORM}/arch-arm/usr/include \
  -I${ANDROID_NDK}/sources/cxx-stl/${STL}/include \
  -I${ANDROID_NDK}/sources/android/support/include \
  -I./include -I. -I./third_party/boringssl/include"
