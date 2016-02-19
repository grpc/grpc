#!/bin/sh

cd $(dirname $0)/../..
cwd=`pwd`

# this requires the debian/ubuntu package gcc-arm-none-eabi

export CC=arm-none-eabi-gcc
export AR="arm-none-eabi-ar rcs"
export CPPFLAGS="-include $cwd/test/exotic/bare-metal.h"

make $cwd/libs/opt/libgrpc_unsecure.a
