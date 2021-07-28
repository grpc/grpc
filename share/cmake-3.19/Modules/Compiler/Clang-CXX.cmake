include(Compiler/Clang)
__compiler_clang(CXX)
__compiler_clang_cxx_standards(CXX)

if("x${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "xGNU")
  set(CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN "-fvisibility-inlines-hidden")
endif()

cmake_policy(GET CMP0025 appleClangPolicy)
if(APPLE AND NOT appleClangPolicy STREQUAL NEW)
  return()
endif()

if("x${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}" STREQUAL "xMSVC")
  set(CMAKE_CXX_CLANG_TIDY_DRIVER_MODE "cl")
endif()
