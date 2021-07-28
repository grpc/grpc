# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
TestForANSIForScope
-------------------

Check for ANSI for scope support

Check if the compiler restricts the scope of variables declared in a
for-init-statement to the loop body.

::

  CMAKE_NO_ANSI_FOR_SCOPE - holds result
#]=======================================================================]

if(NOT DEFINED CMAKE_ANSI_FOR_SCOPE)
  message(CHECK_START "Check for ANSI scope")
  try_compile(CMAKE_ANSI_FOR_SCOPE  ${CMAKE_BINARY_DIR}
    ${CMAKE_ROOT}/Modules/TestForAnsiForScope.cxx
    OUTPUT_VARIABLE OUTPUT)
  if (CMAKE_ANSI_FOR_SCOPE)
    message(CHECK_PASS "found")
    set (CMAKE_NO_ANSI_FOR_SCOPE 0 CACHE INTERNAL
      "Does the compiler support ansi for scope.")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the CXX compiler understands ansi for scopes passed with "
      "the following output:\n${OUTPUT}\n\n")
  else ()
    message(CHECK_FAIL "not found")
    set (CMAKE_NO_ANSI_FOR_SCOPE 1 CACHE INTERNAL
      "Does the compiler support ansi for scope.")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if the CXX compiler understands ansi for scopes failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif ()
endif()





