# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(VerifyFortranC C Fortran)

option(VERIFY_CXX "Whether to verify C++ and Fortran" OFF)
if(VERIFY_CXX)
  enable_language(CXX)
  set(VerifyCXX VerifyCXX.cxx)
  add_definitions(-DVERIFY_CXX)
endif()

include(FortranCInterface)

FortranCInterface_HEADER(VerifyFortran.h SYMBOLS VerifyFortran)
include_directories(${VerifyFortranC_BINARY_DIR})

add_library(VerifyFortran STATIC VerifyFortran.f)
add_executable(VerifyFortranC main.c VerifyC.c ${VerifyCXX})
target_link_libraries(VerifyFortranC VerifyFortran)

if(NOT VERIFY_CXX)
  # The entry point (main) is defined in C; link with the C compiler.
  set_property(TARGET VerifyFortranC PROPERTY LINKER_LANGUAGE C)
endif()
