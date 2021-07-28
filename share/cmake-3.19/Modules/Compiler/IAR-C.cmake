# This file is processed when the IAR compiler is used for a C file

include(Compiler/IAR)
include(Compiler/CMakeCommonCompilerMacros)

# Common
if(NOT CMAKE_C_COMPILER_VERSION)
  message(FATAL_ERROR "CMAKE_C_COMPILER_VERSION not detected.  This should be automatic.")
endif()

set(CMAKE_C_EXTENSION_COMPILE_OPTION -e)

if(CMAKE_C_COMPILER_VERSION_INTERNAL VERSION_GREATER 7)
  set(CMAKE_C90_STANDARD_COMPILE_OPTION --c89)
  set(CMAKE_C90_EXTENSION_COMPILE_OPTION --c89 -e)
  set(CMAKE_C99_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C99_EXTENSION_COMPILE_OPTION -e)
else()
  set(CMAKE_C90_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C90_EXTENSION_COMPILE_OPTION -e)
endif()

if(CMAKE_C_COMPILER_VERSION_INTERNAL VERSION_GREATER 8)
  set(CMAKE_C11_STANDARD_COMPILE_OPTION "")
  set(CMAKE_C11_EXTENSION_COMPILE_OPTION -e)
endif()

# Architecture specific
if("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "ARM")
  if(CMAKE_C_COMPILER_VERSION_INTERNAL VERSION_LESS 7)
    # IAR ARM 4.X uses xlink.exe, detection is not yet implemented
    message(FATAL_ERROR "CMAKE_C_COMPILER_VERSION = ${CMAKE_C_COMPILER_VERSION} not supported by CMake.")
  endif()
  __compiler_iar_ilink(C)
  __compiler_check_default_language_standard(C 1.10 90 6.10 99 8.10 11)

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "RX")
  __compiler_iar_ilink(C)
  __compiler_check_default_language_standard(C 1.10 90 2.10 99 4.10 11)

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "RH850")
  __compiler_iar_ilink(C)
  __compiler_check_default_language_standard(C 1.10 90 1.10 99 2.10 11)

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "RL78")
  __compiler_iar_ilink(C)
  __compiler_check_default_language_standard(C 1.10 90 1.10 99 4.10 11)

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "RISCV")
  __compiler_iar_ilink(C)
  __compiler_check_default_language_standard(C 1.10 90 1.10 99 1.10 11)

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "AVR")
  __compiler_iar_xlink(C)
  __compiler_check_default_language_standard(C 7.10 99)
  set(CMAKE_C_OUTPUT_EXTENSION ".r90")

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "MSP430")
  __compiler_iar_xlink(C)
  __compiler_check_default_language_standard(C 1.10 90 5.10 99)
  set(CMAKE_C_OUTPUT_EXTENSION ".r43")

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "V850")
  __compiler_iar_xlink(C)
  __compiler_check_default_language_standard(C 1.10 90 4.10 99)
  set(CMAKE_C_OUTPUT_EXTENSION ".r85")

elseif("${CMAKE_C_COMPILER_ARCHITECTURE_ID}" STREQUAL "8051")
  __compiler_iar_xlink(C)
  __compiler_check_default_language_standard(C 6.10 90 8.10 99)
  set(CMAKE_C_OUTPUT_EXTENSION ".r51")

else()
  message(FATAL_ERROR "CMAKE_C_COMPILER_ARCHITECTURE_ID not detected. This should be automatic.")
endif()
