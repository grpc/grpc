include(Platform/Linux)

set(ANDROID 1)

# Natively compiling on an Android host doesn't need these flags to be reset.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Android")
  return()
endif()

# Conventionally Android does not use versioned soname
# But in modern versions it is acceptable
if(NOT DEFINED CMAKE_PLATFORM_NO_VERSIONED_SONAME)
  set(CMAKE_PLATFORM_NO_VERSIONED_SONAME 1)
endif()

# Android reportedly ignores RPATH, and we cannot predict the install
# location anyway.
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "")

# Nsight Tegra Visual Studio Edition takes care of
# prefixing library names with '-l'.
if(CMAKE_VS_PLATFORM_NAME STREQUAL "Tegra-Android")
  set(CMAKE_LINK_LIBRARY_FLAG "")
endif()
