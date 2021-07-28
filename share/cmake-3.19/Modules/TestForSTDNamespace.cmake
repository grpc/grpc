# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
TestForSTDNamespace
-------------------

Test for std:: namespace support

check if the compiler supports std:: on stl classes

::

  CMAKE_NO_STD_NAMESPACE - defined by the results
#]=======================================================================]

if(NOT DEFINED CMAKE_STD_NAMESPACE)
  message(CHECK_START "Check for STD namespace")
  try_compile(CMAKE_STD_NAMESPACE  ${CMAKE_BINARY_DIR}
    ${CMAKE_ROOT}/Modules/TestForSTDNamespace.cxx
    OUTPUT_VARIABLE OUTPUT)
  if (CMAKE_STD_NAMESPACE)
    message(CHECK_PASS "found")
    set (CMAKE_NO_STD_NAMESPACE 0 CACHE INTERNAL
         "Does the compiler support std::.")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the CXX compiler has std namespace passed with "
      "the following output:\n${OUTPUT}\n\n")
  else ()
    message(CHECK_FAIL "not found")
    set (CMAKE_NO_STD_NAMESPACE 1 CACHE INTERNAL
       "Does the compiler support std::.")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Determining if the CXX compiler has std namespace failed with "
      "the following output:\n${OUTPUT}\n\n")
  endif ()
endif()




