# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This file sets the basic flags for the C language in CMake.
# It also loads the available platform file for the system-compiler
# if it exists.
# It also loads a system - compiler - processor (or target hardware)
# specific file, which is mainly useful for crosscompiling and embedded systems.

include(CMakeLanguageInformation)

# some compilers use different extensions (e.g. sdcc uses .rel)
# so set the extension here first so it can be overridden by the compiler specific file
if(UNIX)
  set(CMAKE_C_OUTPUT_EXTENSION .o)
else()
  set(CMAKE_C_OUTPUT_EXTENSION .obj)
endif()

set(_INCLUDED_FILE 0)

# Load compiler-specific information.
if(CMAKE_C_COMPILER_ID)
  include(Compiler/${CMAKE_C_COMPILER_ID}-C OPTIONAL)
endif()

set(CMAKE_BASE_NAME)
get_filename_component(CMAKE_BASE_NAME "${CMAKE_C_COMPILER}" NAME_WE)
if(CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_BASE_NAME gcc)
endif()


# load a hardware specific file, mostly useful for embedded compilers
if(CMAKE_SYSTEM_PROCESSOR)
  if(CMAKE_C_COMPILER_ID)
    include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_C_COMPILER_ID}-C-${CMAKE_SYSTEM_PROCESSOR} OPTIONAL RESULT_VARIABLE _INCLUDED_FILE)
  endif()
  if (NOT _INCLUDED_FILE)
    include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_BASE_NAME}-${CMAKE_SYSTEM_PROCESSOR} OPTIONAL)
  endif ()
endif()


# load the system- and compiler specific files
if(CMAKE_C_COMPILER_ID)
  include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_C_COMPILER_ID}-C
    OPTIONAL RESULT_VARIABLE _INCLUDED_FILE)
endif()
if (NOT _INCLUDED_FILE)
  include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_BASE_NAME}
    OPTIONAL RESULT_VARIABLE _INCLUDED_FILE)
endif ()

# load any compiler-wrapper specific information
if (CMAKE_C_COMPILER_WRAPPER)
  __cmake_include_compiler_wrapper(C)
endif ()

# We specify the compiler information in the system file for some
# platforms, but this language may not have been enabled when the file
# was first included.  Include it again to get the language info.
# Remove this when all compiler info is removed from system files.
if (NOT _INCLUDED_FILE)
  include(Platform/${CMAKE_SYSTEM_NAME} OPTIONAL)
endif ()

if(CMAKE_C_SIZEOF_DATA_PTR)
  foreach(f ${CMAKE_C_ABI_FILES})
    include(${f})
  endforeach()
  unset(CMAKE_C_ABI_FILES)
endif()

# This should be included before the _INIT variables are
# used to initialize the cache.  Since the rule variables
# have if blocks on them, users can still define them here.
# But, it should still be after the platform file so changes can
# be made to those values.

if(CMAKE_USER_MAKE_RULES_OVERRIDE)
  # Save the full path of the file so try_compile can use it.
  include(${CMAKE_USER_MAKE_RULES_OVERRIDE} RESULT_VARIABLE _override)
  set(CMAKE_USER_MAKE_RULES_OVERRIDE "${_override}")
endif()

if(CMAKE_USER_MAKE_RULES_OVERRIDE_C)
  # Save the full path of the file so try_compile can use it.
  include(${CMAKE_USER_MAKE_RULES_OVERRIDE_C} RESULT_VARIABLE _override)
  set(CMAKE_USER_MAKE_RULES_OVERRIDE_C "${_override}")
endif()


# for most systems a module is the same as a shared library
# so unless the variable CMAKE_MODULE_EXISTS is set just
# copy the values from the LIBRARY variables
if(NOT CMAKE_MODULE_EXISTS)
  set(CMAKE_SHARED_MODULE_C_FLAGS ${CMAKE_SHARED_LIBRARY_C_FLAGS})
  set(CMAKE_SHARED_MODULE_CREATE_C_FLAGS ${CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS})
endif()

set(CMAKE_C_FLAGS_INIT "$ENV{CFLAGS} ${CMAKE_C_FLAGS_INIT}")

cmake_initialize_per_config_variable(CMAKE_C_FLAGS "Flags used by the C compiler")

if(CMAKE_C_STANDARD_LIBRARIES_INIT)
  set(CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES_INIT}"
    CACHE STRING "Libraries linked by default with all C applications.")
  mark_as_advanced(CMAKE_C_STANDARD_LIBRARIES)
endif()

if(NOT CMAKE_C_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_C_COMPILER_LAUNCHER})
  set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}"
    CACHE STRING "Compiler launcher for C.")
endif()

include(CMakeCommonLanguageInclude)

# now define the following rule variables

# CMAKE_C_CREATE_SHARED_LIBRARY
# CMAKE_C_CREATE_SHARED_MODULE
# CMAKE_C_COMPILE_OBJECT
# CMAKE_C_LINK_EXECUTABLE

# variables supplied by the generator at use time
# <TARGET>
# <TARGET_BASE> the target without the suffix
# <OBJECTS>
# <OBJECT>
# <LINK_LIBRARIES>
# <FLAGS>
# <LINK_FLAGS>

# C compiler information
# <CMAKE_C_COMPILER>
# <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS>
# <CMAKE_SHARED_MODULE_CREATE_C_FLAGS>
# <CMAKE_C_LINK_FLAGS>

# Static library tools
# <CMAKE_AR>
# <CMAKE_RANLIB>


# create a C shared library
if(NOT CMAKE_C_CREATE_SHARED_LIBRARY)
  set(CMAKE_C_CREATE_SHARED_LIBRARY
      "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
endif()

# create a C shared module just copy the shared library rule
if(NOT CMAKE_C_CREATE_SHARED_MODULE)
  set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
endif()

# Create a static archive incrementally for large object file counts.
# If CMAKE_C_CREATE_STATIC_LIBRARY is set it will override these.
if(NOT DEFINED CMAKE_C_ARCHIVE_CREATE)
  set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()
if(NOT DEFINED CMAKE_C_ARCHIVE_APPEND)
  set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> q <TARGET> <LINK_FLAGS> <OBJECTS>")
endif()
if(NOT DEFINED CMAKE_C_ARCHIVE_FINISH)
  set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")
endif()

# compile a C file into an object file
if(NOT CMAKE_C_COMPILE_OBJECT)
  set(CMAKE_C_COMPILE_OBJECT
    "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
endif()

if(NOT CMAKE_C_LINK_EXECUTABLE)
  set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
endif()

if(NOT CMAKE_EXECUTABLE_RUNTIME_C_FLAG)
  set(CMAKE_EXECUTABLE_RUNTIME_C_FLAG ${CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG})
endif()

if(NOT CMAKE_EXECUTABLE_RUNTIME_C_FLAG_SEP)
  set(CMAKE_EXECUTABLE_RUNTIME_C_FLAG_SEP ${CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP})
endif()

if(NOT CMAKE_EXECUTABLE_RPATH_LINK_C_FLAG)
  set(CMAKE_EXECUTABLE_RPATH_LINK_C_FLAG ${CMAKE_SHARED_LIBRARY_RPATH_LINK_C_FLAG})
endif()

set(CMAKE_C_INFORMATION_LOADED 1)


