# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeBackwardCompatibilityCXX
-----------------------------

define a bunch of backwards compatibility variables

::

  CMAKE_ANSI_CXXFLAGS - flag for ansi c++
  CMAKE_HAS_ANSI_STRING_STREAM - has <strstream>
  include(TestForANSIStreamHeaders)
  include(CheckIncludeFileCXX)
  include(TestForSTDNamespace)
  include(TestForANSIForScope)
#]=======================================================================]

if(NOT CMAKE_SKIP_COMPATIBILITY_TESTS)
  # check for some ANSI flags in the CXX compiler if it is not gnu
  if(NOT CMAKE_COMPILER_IS_GNUCXX)
    include(TestCXXAcceptsFlag)
    set(CMAKE_TRY_ANSI_CXX_FLAGS "")
    if(CMAKE_SYSTEM_NAME MATCHES "OSF")
      set(CMAKE_TRY_ANSI_CXX_FLAGS "-std strict_ansi -nopure_cname")
    endif()
    # if CMAKE_TRY_ANSI_CXX_FLAGS has something in it, see
    # if the compiler accepts it
    if(NOT CMAKE_TRY_ANSI_CXX_FLAGS STREQUAL "")
      CHECK_CXX_ACCEPTS_FLAG(${CMAKE_TRY_ANSI_CXX_FLAGS} CMAKE_CXX_ACCEPTS_FLAGS)
      # if the compiler liked the flag then set CMAKE_ANSI_CXXFLAGS
      # to the flag
      if(CMAKE_CXX_ACCEPTS_FLAGS)
        set(CMAKE_ANSI_CXXFLAGS ${CMAKE_TRY_ANSI_CXX_FLAGS} CACHE INTERNAL
        "What flags are required by the c++ compiler to make it ansi." )
      endif()
    endif()
  endif()
  set(CMAKE_CXX_FLAGS_SAVE ${CMAKE_CXX_FLAGS})
  string(APPEND CMAKE_CXX_FLAGS " ${CMAKE_ANSI_CXXFLAGS}")
  include(TestForANSIStreamHeaders)
  include(CheckIncludeFileCXX)
  include(TestForSTDNamespace)
  include(TestForANSIForScope)
  include(TestForSSTREAM)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_SAVE}")
endif()

