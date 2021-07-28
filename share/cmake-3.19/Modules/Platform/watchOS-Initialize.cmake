include(Platform/Darwin-Initialize)

if(NOT _CMAKE_OSX_SYSROOT_PATH MATCHES "/Watch(OS|Simulator)")
  message(FATAL_ERROR "${CMAKE_OSX_SYSROOT} is not an watchOS SDK")
endif()

set(_CMAKE_FEATURE_DETECTION_TARGET_TYPE STATIC_LIBRARY)
