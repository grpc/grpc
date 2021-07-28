# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This file is included in CMakeSystemSpecificInformation.cmake if
# the Eclipse CDT4 extra generator has been selected.


# Try to find out how many CPUs we have and set the -j argument for make accordingly

include(ProcessorCount)
processorcount(_CMAKE_KATE_PROCESSOR_COUNT)

# Only set -j if we are under UNIX and if the make-tool used actually has "make" in the name
# (we may also get here in the future e.g. for ninja)
if("${_CMAKE_KATE_PROCESSOR_COUNT}" GREATER 1  AND  CMAKE_HOST_UNIX  AND  "${CMAKE_MAKE_PROGRAM}" MATCHES make)
  set(_CMAKE_KATE_INITIAL_MAKE_ARGS "-j${_CMAKE_KATE_PROCESSOR_COUNT}")
endif()

# This variable is used by the Eclipse generator and appended to the make invocation commands.
set(CMAKE_KATE_MAKE_ARGUMENTS "${_CMAKE_KATE_INITIAL_MAKE_ARGS}" CACHE STRING "Additional command line arguments when Kate invokes make. Enter e.g. -j<some_number> to get parallel builds")
