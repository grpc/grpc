# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindFLTK2
---------

Find the native FLTK 2.0 includes and library

The following settings are defined

::

  FLTK2_FLUID_EXECUTABLE, where to find the Fluid tool
  FLTK2_WRAP_UI, This enables the FLTK2_WRAP_UI command
  FLTK2_INCLUDE_DIR, where to find include files
  FLTK2_LIBRARIES, list of fltk2 libraries
  FLTK2_FOUND, Don't use FLTK2 if false.

The following settings should not be used in general.

::

  FLTK2_BASE_LIBRARY   = the full path to fltk2.lib
  FLTK2_GL_LIBRARY     = the full path to fltk2_gl.lib
  FLTK2_IMAGES_LIBRARY = the full path to fltk2_images.lib
#]=======================================================================]

set (FLTK2_DIR $ENV{FLTK2_DIR} )

#  Platform dependent libraries required by FLTK2
if(WIN32)
  if(NOT CYGWIN)
    if(BORLAND)
      set( FLTK2_PLATFORM_DEPENDENT_LIBS import32 )
    else()
      set( FLTK2_PLATFORM_DEPENDENT_LIBS wsock32 comctl32 )
    endif()
  endif()
endif()

if(UNIX)
  include(${CMAKE_ROOT}/Modules/FindX11.cmake)
  set( FLTK2_PLATFORM_DEPENDENT_LIBS ${X11_LIBRARIES} -lm)
endif()

if(APPLE)
  set( FLTK2_PLATFORM_DEPENDENT_LIBS  "-framework Carbon -framework Cocoa -framework ApplicationServices -lz")
endif()

# If FLTK2_INCLUDE_DIR is already defined we assign its value to FLTK2_DIR
if(FLTK2_INCLUDE_DIR)
  set(FLTK2_DIR ${FLTK2_INCLUDE_DIR})
else()
  set(FLTK2_INCLUDE_DIR ${FLTK2_DIR})
endif()


# If FLTK2 has been built using CMake we try to find everything directly
set(FLTK2_DIR_STRING "directory containing FLTK2Config.cmake.  This is either the root of the build tree, or PREFIX/lib/fltk for an installation.")

# Search only if the location is not already known.
if(NOT FLTK2_DIR)
  # Get the system search path as a list.
  file(TO_CMAKE_PATH "$ENV{PATH}" FLTK2_DIR_SEARCH2)

  # Construct a set of paths relative to the system search path.
  set(FLTK2_DIR_SEARCH "")
  foreach(dir ${FLTK2_DIR_SEARCH2})
    set(FLTK2_DIR_SEARCH ${FLTK2_DIR_SEARCH} "${dir}/../lib/fltk")
  endforeach()
  string(REPLACE "//" "/" FLTK2_DIR_SEARCH "${FLTK2_DIR_SEARCH}")

  #
  # Look for an installation or build tree.
  #
  find_path(FLTK2_DIR FLTK2Config.cmake
    # Look for an environment variable FLTK2_DIR.
    ENV FLTK2_DIR

    # Look in places relative to the system executable search path.
    ${FLTK2_DIR_SEARCH}

    PATH_SUFFIXES
    fltk2
    fltk2/include
    lib/fltk2
    lib/fltk2/include

    # Help the user find it if we cannot.
    DOC "The ${FLTK2_DIR_STRING}"
    )

  if(NOT FLTK2_DIR)
    find_path(FLTK2_DIR fltk/run.h ${FLTK2_INCLUDE_SEARCH_PATH})
  endif()

endif()


# If FLTK2 was found, load the configuration file to get the rest of the
# settings.
if(FLTK2_DIR)

  # Check if FLTK2 was built using CMake
  if(EXISTS ${FLTK2_DIR}/FLTK2Config.cmake)
    set(FLTK2_BUILT_WITH_CMAKE 1)
  endif()

  if(FLTK2_BUILT_WITH_CMAKE)
    set(FLTK2_FOUND 1)
    include(${FLTK2_DIR}/FLTK2Config.cmake)

    # Fluid
    if(FLUID_COMMAND)
      set(FLTK2_FLUID_EXECUTABLE ${FLUID_COMMAND} CACHE FILEPATH "Fluid executable")
    else()
      find_program(FLTK2_FLUID_EXECUTABLE fluid2 PATHS
        ${FLTK2_EXECUTABLE_DIRS}
        ${FLTK2_EXECUTABLE_DIRS}/RelWithDebInfo
        ${FLTK2_EXECUTABLE_DIRS}/Debug
        ${FLTK2_EXECUTABLE_DIRS}/Release
        NO_SYSTEM_PATH)
    endif()

    mark_as_advanced(FLTK2_FLUID_EXECUTABLE)
    set( FLTK_FLUID_EXECUTABLE ${FLTK2_FLUID_EXECUTABLE} )




    set(FLTK2_INCLUDE_DIR ${FLTK2_DIR})
    link_directories(${FLTK2_LIBRARY_DIRS})

    set(FLTK2_BASE_LIBRARY fltk2)
    set(FLTK2_GL_LIBRARY fltk2_gl)
    set(FLTK2_IMAGES_LIBRARY fltk2_images)

    # Add the extra libraries
    load_cache(${FLTK2_DIR}
      READ_WITH_PREFIX
      FL FLTK2_USE_SYSTEM_JPEG
      FL FLTK2_USE_SYSTEM_PNG
      FL FLTK2_USE_SYSTEM_ZLIB
      )

    set(FLTK2_IMAGES_LIBS "")
    if(FLFLTK2_USE_SYSTEM_JPEG)
      set(FLTK2_IMAGES_LIBS ${FLTK2_IMAGES_LIBS} fltk2_jpeg)
    endif()
    if(FLFLTK2_USE_SYSTEM_PNG)
      set(FLTK2_IMAGES_LIBS ${FLTK2_IMAGES_LIBS} fltk2_png)
    endif()
    if(FLFLTK2_USE_SYSTEM_ZLIB)
      set(FLTK2_IMAGES_LIBS ${FLTK2_IMAGES_LIBS} fltk2_zlib)
    endif()
    set(FLTK2_IMAGES_LIBS "${FLTK2_IMAGES_LIBS}" CACHE INTERNAL
      "Extra libraries for fltk2_images library.")

  else()

    # if FLTK2 was not built using CMake
    # Find fluid executable.
    find_program(FLTK2_FLUID_EXECUTABLE fluid2 ${FLTK2_INCLUDE_DIR}/fluid)

    # Use location of fluid to help find everything else.
    set(FLTK2_INCLUDE_SEARCH_PATH "")
    set(FLTK2_LIBRARY_SEARCH_PATH "")
    if(FLTK2_FLUID_EXECUTABLE)
      set( FLTK_FLUID_EXECUTABLE ${FLTK2_FLUID_EXECUTABLE} )
      get_filename_component(FLTK2_BIN_DIR "${FLTK2_FLUID_EXECUTABLE}" PATH)
      set(FLTK2_INCLUDE_SEARCH_PATH ${FLTK2_INCLUDE_SEARCH_PATH}
        ${FLTK2_BIN_DIR}/../include ${FLTK2_BIN_DIR}/..)
      set(FLTK2_LIBRARY_SEARCH_PATH ${FLTK2_LIBRARY_SEARCH_PATH}
        ${FLTK2_BIN_DIR}/../lib)
      set(FLTK2_WRAP_UI 1)
    endif()

    find_path(FLTK2_INCLUDE_DIR fltk/run.h ${FLTK2_INCLUDE_SEARCH_PATH} PATH_SUFFIXES fltk2 fltk2/include)

    list(APPEND FLTK2_LIBRARY_SEARCH_PATH ${FLTK2_INCLUDE_DIR}/lib)

    find_library(FLTK2_BASE_LIBRARY NAMES fltk2
      PATHS ${FLTK2_LIBRARY_SEARCH_PATH} PATH_SUFFIXES fltk2 fltk2/lib)
    find_library(FLTK2_GL_LIBRARY NAMES fltk2_gl
      PATHS ${FLTK2_LIBRARY_SEARCH_PATH} PATH_SUFFIXES fltk2 fltk2/lib)
    find_library(FLTK2_IMAGES_LIBRARY NAMES fltk2_images
      PATHS ${FLTK2_LIBRARY_SEARCH_PATH} PATH_SUFFIXES fltk2 fltk2/lib)

    # Find the extra libraries needed for the fltk_images library.
    if(UNIX)
      find_program(FLTK2_CONFIG_SCRIPT fltk2-config PATHS ${FLTK2_BIN_DIR})
      if(FLTK2_CONFIG_SCRIPT)
        exec_program(${FLTK2_CONFIG_SCRIPT} ARGS --use-images --ldflags
          OUTPUT_VARIABLE FLTK2_IMAGES_LDFLAGS)
        set(FLTK2_LIBS_EXTRACT_REGEX ".*-lfltk2_images (.*) -lfltk2.*")
        if("${FLTK2_IMAGES_LDFLAGS}" MATCHES "${FLTK2_LIBS_EXTRACT_REGEX}")
          string(REGEX REPLACE " +" ";" FLTK2_IMAGES_LIBS "${CMAKE_MATCH_1}")
          # The EXEC_PROGRAM will not be inherited into subdirectories from
          # the file that originally included this module.  Save the answer.
          set(FLTK2_IMAGES_LIBS "${FLTK2_IMAGES_LIBS}" CACHE INTERNAL
            "Extra libraries for fltk_images library.")
        endif()
      endif()
    endif()

  endif()
endif()


set(FLTK2_FOUND 1)
foreach(var FLTK2_FLUID_EXECUTABLE FLTK2_INCLUDE_DIR
    FLTK2_BASE_LIBRARY FLTK2_GL_LIBRARY
    FLTK2_IMAGES_LIBRARY)
  if(NOT ${var})
    message( STATUS "${var} not found" )
    set(FLTK2_FOUND 0)
  endif()
endforeach()


if(FLTK2_FOUND)
  set(FLTK2_LIBRARIES ${FLTK2_IMAGES_LIBRARY} ${FLTK2_IMAGES_LIBS} ${FLTK2_BASE_LIBRARY} ${FLTK2_GL_LIBRARY} )
  if(APPLE)
    set(FLTK2_LIBRARIES ${FLTK2_PLATFORM_DEPENDENT_LIBS} ${FLTK2_LIBRARIES})
  else()
    set(FLTK2_LIBRARIES ${FLTK2_LIBRARIES} ${FLTK2_PLATFORM_DEPENDENT_LIBS})
  endif()

  # The following deprecated settings are for compatibility with CMake 1.4
  set (HAS_FLTK2 ${FLTK2_FOUND})
  set (FLTK2_INCLUDE_PATH ${FLTK2_INCLUDE_DIR})
  set (FLTK2_FLUID_EXE ${FLTK2_FLUID_EXECUTABLE})
  set (FLTK2_LIBRARY ${FLTK2_LIBRARIES})
else()
  # make FIND_PACKAGE friendly
  if(NOT FLTK2_FIND_QUIETLY)
    if(FLTK2_FIND_REQUIRED)
      message(FATAL_ERROR
              "FLTK2 required, please specify its location with FLTK2_DIR.")
    else()
      message(STATUS "FLTK2 was not found.")
    endif()
  endif()
endif()
