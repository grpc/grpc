# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
TestForANSIStreamHeaders
------------------------

Test for compiler support of ANSI stream headers iostream, etc.

check if the compiler supports the standard ANSI iostream header
(without the .h)

::

  CMAKE_NO_ANSI_STREAM_HEADERS - defined by the results
#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/CheckIncludeFileCXX.cmake)

if(NOT CMAKE_NO_ANSI_STREAM_HEADERS)
  CHECK_INCLUDE_FILE_CXX(iostream CMAKE_ANSI_STREAM_HEADERS)
  if (CMAKE_ANSI_STREAM_HEADERS)
    set (CMAKE_NO_ANSI_STREAM_HEADERS 0 CACHE INTERNAL
         "Does the compiler support headers like iostream.")
  else ()
    set (CMAKE_NO_ANSI_STREAM_HEADERS 1 CACHE INTERNAL
       "Does the compiler support headers like iostream.")
  endif ()

  mark_as_advanced(CMAKE_NO_ANSI_STREAM_HEADERS)
endif()


