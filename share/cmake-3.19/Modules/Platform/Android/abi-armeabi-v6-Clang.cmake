# <ndk>/build/core/toolchains/arm-linux-androideabi-clang/setup.mk
set(_ANDROID_ABI_CLANG_TARGET "armv6-none-linux-androideabi")

string(APPEND _ANDROID_ABI_INIT_CFLAGS
  " -march=armv6"
  )

if(CMAKE_ANDROID_ARM_MODE)
  string(APPEND _ANDROID_ABI_INIT_CFLAGS " -marm")
else()
  string(APPEND _ANDROID_ABI_INIT_CFLAGS " -mthumb")
endif()

string(APPEND _ANDROID_ABI_INIT_CFLAGS
  " -mfloat-abi=softfp"
  )

include(Platform/Android/abi-common-Clang)
