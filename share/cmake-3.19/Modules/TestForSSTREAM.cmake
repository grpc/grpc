# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
TestForSSTREAM
--------------

Test for compiler support of ANSI sstream header

check if the compiler supports the standard ANSI sstream header

::

  CMAKE_NO_ANSI_STRING_STREAM - defined by the results
#]=======================================================================]

if(NOT DEFINED CMAKE_HAS_ANSI_STRING_STREAM)
  message(CHECK_START "Check for sstream")
  try_compile(CMAKE_HAS_ANSI_STRING_STREAM  ${CMAKE_BINARY_DIR}
    ${CMAKE_ROOT}/Modules/TestForSSTREAM.cxx
    OUTPUT_VARIABLE OUTPUT)
  if (CMAKE_HAS_ANSI_STRING_STREAM)
    message(CHECK_PASS "found")
    set (CMAKE_NO_ANSI_STRING_STREAM 0 CACHE INTERNAL
         "Does the compiler support sstream")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the CXX compiler has sstream passed with "
      "the following output:\n${OUTPUT}\n\n")
  else ()
    message(CHECK_FAIL "not found")
    set (CMAKE_NO_ANSI_STRING_STREAM 1 CACHE INTERNAL
       "Does the compiler support sstream")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if the CXX compiler has sstream failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif ()
endif()




