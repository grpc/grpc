# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This file is included by cmGlobalGenerator::EnableLanguage.
# It is included after the compiler has been determined, so
# we know things like the compiler name and if the compiler is gnu.

# before cmake 2.6 these variables were set in cmMakefile.cxx. This is still
# done to keep scripts and custom language and compiler modules working.
# But they are reset here and set again in the platform files for the target
# platform, so they can be used for testing the target platform instead
# of testing the host platform.
set(APPLE  )
set(UNIX   )
set(CYGWIN )
set(WIN32  )


# include Generic system information
include(CMakeGenericSystem)

# 2. now include SystemName.cmake file to set the system specific information
set(CMAKE_SYSTEM_INFO_FILE Platform/${CMAKE_SYSTEM_NAME})

include(${CMAKE_SYSTEM_INFO_FILE} OPTIONAL RESULT_VARIABLE _INCLUDED_SYSTEM_INFO_FILE)

if(NOT _INCLUDED_SYSTEM_INFO_FILE)
  message("System is unknown to cmake, create:\n${CMAKE_SYSTEM_INFO_FILE}"
          " to use this system, please post your config file on "
          "discourse.cmake.org so it can be added to cmake")
  if(EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
    configure_file(${CMAKE_BINARY_DIR}/CMakeCache.txt
                   ${CMAKE_BINARY_DIR}/CopyOfCMakeCache.txt COPYONLY)
    message("Your CMakeCache.txt file was copied to CopyOfCMakeCache.txt. "
            "Please post that file on discourse.cmake.org.")
  endif()
endif()

# optionally include a file which can do extra-generator specific things, e.g.
# CMakeFindEclipseCDT4.cmake asks gcc for the system include dirs for the Eclipse CDT4 generator
if(CMAKE_EXTRA_GENERATOR)
  string(REPLACE " " "" _CMAKE_EXTRA_GENERATOR_NO_SPACES ${CMAKE_EXTRA_GENERATOR} )
  include("CMakeFind${_CMAKE_EXTRA_GENERATOR_NO_SPACES}" OPTIONAL)
endif()


# for most systems a module is the same as a shared library
# so unless the variable CMAKE_MODULE_EXISTS is set just
# copy the values from the LIBRARY variables
# this has to be done after the system information has been loaded
if(NOT CMAKE_MODULE_EXISTS)
  set(CMAKE_SHARED_MODULE_PREFIX "${CMAKE_SHARED_LIBRARY_PREFIX}")
  set(CMAKE_SHARED_MODULE_SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()


set(CMAKE_SYSTEM_SPECIFIC_INFORMATION_LOADED 1)
