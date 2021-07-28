# This file is processed when the IAR compiler is used for a C++ file

include(Compiler/IAR)
include(Compiler/CMakeCommonCompilerMacros)

# Common
if(NOT CMAKE_CXX_COMPILER_VERSION)
  message(FATAL_ERROR "CMAKE_CXX_COMPILER_VERSION not detected. This should be automatic.")
endif()

if(NOT CMAKE_IAR_CXX_FLAG)
  # The --c++ flag was introduced in platform version 9 for all architectures except ARM where it was introduced already in version 7
  if(CMAKE_CXX_COMPILER_VERSION_INTERNAL VERSION_GREATER 8 OR
    (CMAKE_CXX_COMPILER_VERSION_INTERNAL VERSION_GREATER 6 AND "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "ARM") )
    set(CMAKE_IAR_CXX_FLAG --c++)
  else()
    set(CMAKE_IAR_CXX_FLAG --eec++)
  endif()
endif()

set(CMAKE_CXX_EXTENSION_COMPILE_OPTION -e)

if(CMAKE_CXX_COMPILER_VERSION_INTERNAL VERSION_GREATER 7)
  set(CMAKE_CXX98_STANDARD_COMPILE_OPTION "")
  set(CMAKE_CXX98_EXTENSION_COMPILE_OPTION -e)
  set(CMAKE_CXX03_STANDARD_COMPILE_OPTION "")
  set(CMAKE_CXX03_EXTENSION_COMPILE_OPTION -e)
endif()

if(CMAKE_CXX_COMPILER_VERSION_INTERNAL VERSION_GREATER 8)
  set(CMAKE_CXX11_STANDARD_COMPILE_OPTION "")
  set(CMAKE_CXX11_EXTENSION_COMPILE_OPTION -e)
  set(CMAKE_CXX14_STANDARD_COMPILE_OPTION "")
  set(CMAKE_CXX14_EXTENSION_COMPILE_OPTION -e)
endif()

# Architecture specific
if("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "ARM")
  if(CMAKE_CXX_COMPILER_VERSION_INTERNAL VERSION_LESS 7)
    # IAR ARM 4.X uses xlink.exe, detection is not yet implemented
    message(FATAL_ERROR "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_C_COMPILER_VERSION} not supported by CMake.")
  endif()
  __compiler_iar_ilink(CXX)
  __compiler_check_default_language_standard(CXX 6.10 98 8.10 14)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "RX")
  __compiler_iar_ilink(CXX)
  __compiler_check_default_language_standard(CXX 2.10 98 4.10 14)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "RH850")
  __compiler_iar_ilink(CXX)
  __compiler_check_default_language_standard(CXX 1.10 98 2.10 14)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "RL78")
  __compiler_iar_ilink(CXX)
  __compiler_check_default_language_standard(CXX 1.10 98 4.10 14)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "RISCV")
  __compiler_iar_ilink(CXX)
  __compiler_check_default_language_standard(CXX 1.10 98 1.10 14)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "AVR")
  __compiler_iar_xlink(CXX)
  __compiler_check_default_language_standard(CXX 7.10 98)

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "MSP430")
  __compiler_iar_xlink(CXX)
  __compiler_check_default_language_standard(CXX 5.10 98)
  set(CMAKE_CXX_OUTPUT_EXTENSION ".r43")

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "V850")
  __compiler_iar_xlink(CXX)
  __compiler_check_default_language_standard(CXX 1.10 98)
  set(CMAKE_C_OUTPUT_EXTENSION ".r85")

elseif("${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" STREQUAL "8051")
  __compiler_iar_xlink(CXX)
  __compiler_check_default_language_standard(CXX 6.10 98)
  set(CMAKE_C_OUTPUT_EXTENSION ".r51")

else()
  message(FATAL_ERROR "CMAKE_CXX_COMPILER_ARCHITECTURE_ID not detected. This should be automatic." )
endif()
