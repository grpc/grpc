# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindArmadillo
-------------

Find the Armadillo C++ library.
Armadillo is a library for linear algebra & scientific computing.

Using Armadillo:

::

  find_package(Armadillo REQUIRED)
  include_directories(${ARMADILLO_INCLUDE_DIRS})
  add_executable(foo foo.cc)
  target_link_libraries(foo ${ARMADILLO_LIBRARIES})

This module sets the following variables:

::

  ARMADILLO_FOUND - set to true if the library is found
  ARMADILLO_INCLUDE_DIRS - list of required include directories
  ARMADILLO_LIBRARIES - list of libraries to be linked
  ARMADILLO_VERSION_MAJOR - major version number
  ARMADILLO_VERSION_MINOR - minor version number
  ARMADILLO_VERSION_PATCH - patch version number
  ARMADILLO_VERSION_STRING - version number as a string (ex: "1.0.4")
  ARMADILLO_VERSION_NAME - name of the version (ex: "Antipodean Antileech")
#]=======================================================================]

find_path(ARMADILLO_INCLUDE_DIR
  NAMES armadillo
  PATHS "$ENV{ProgramFiles}/Armadillo/include"
  )
mark_as_advanced(ARMADILLO_INCLUDE_DIR)

if(ARMADILLO_INCLUDE_DIR)
  # ------------------------------------------------------------------------
  #  Extract version information from <armadillo>
  # ------------------------------------------------------------------------

  # WARNING: Early releases of Armadillo didn't have the arma_version.hpp file.
  # (e.g. v.0.9.8-1 in ubuntu maverick packages (2001-03-15))
  # If the file is missing, set all values to 0
  set(ARMADILLO_VERSION_MAJOR 0)
  set(ARMADILLO_VERSION_MINOR 0)
  set(ARMADILLO_VERSION_PATCH 0)
  set(ARMADILLO_VERSION_NAME "EARLY RELEASE")

  if(EXISTS "${ARMADILLO_INCLUDE_DIR}/armadillo_bits/arma_version.hpp")

    # Read and parse armdillo version header file for version number
    file(STRINGS "${ARMADILLO_INCLUDE_DIR}/armadillo_bits/arma_version.hpp" _ARMA_HEADER_CONTENTS REGEX "#define ARMA_VERSION_[A-Z]+ ")
    string(REGEX REPLACE ".*#define ARMA_VERSION_MAJOR ([0-9]+).*" "\\1" ARMADILLO_VERSION_MAJOR "${_ARMA_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define ARMA_VERSION_MINOR ([0-9]+).*" "\\1" ARMADILLO_VERSION_MINOR "${_ARMA_HEADER_CONTENTS}")
    string(REGEX REPLACE ".*#define ARMA_VERSION_PATCH ([0-9]+).*" "\\1" ARMADILLO_VERSION_PATCH "${_ARMA_HEADER_CONTENTS}")

    # WARNING: The number of spaces before the version name is not one.
    string(REGEX REPLACE ".*#define ARMA_VERSION_NAME\ +\"([0-9a-zA-Z\ _-]+)\".*" "\\1" ARMADILLO_VERSION_NAME "${_ARMA_HEADER_CONTENTS}")

  endif()

  set(ARMADILLO_VERSION_STRING "${ARMADILLO_VERSION_MAJOR}.${ARMADILLO_VERSION_MINOR}.${ARMADILLO_VERSION_PATCH}")
endif ()

if(EXISTS "${ARMADILLO_INCLUDE_DIR}/armadillo_bits/config.hpp")
  file(STRINGS "${ARMADILLO_INCLUDE_DIR}/armadillo_bits/config.hpp" _ARMA_CONFIG_CONTENTS REGEX "^#define ARMA_USE_[A-Z]+")
  string(REGEX MATCH "ARMA_USE_WRAPPER" _ARMA_USE_WRAPPER "${_ARMA_CONFIG_CONTENTS}")
  string(REGEX MATCH "ARMA_USE_LAPACK" _ARMA_USE_LAPACK "${_ARMA_CONFIG_CONTENTS}")
  string(REGEX MATCH "ARMA_USE_BLAS" _ARMA_USE_BLAS "${_ARMA_CONFIG_CONTENTS}")
  string(REGEX MATCH "ARMA_USE_ARPACK" _ARMA_USE_ARPACK "${_ARMA_CONFIG_CONTENTS}")
  string(REGEX MATCH "ARMA_USE_HDF5" _ARMA_USE_HDF5 "${_ARMA_CONFIG_CONTENTS}")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

# If _ARMA_USE_WRAPPER is set, then we just link to armadillo, but if it's not then we need support libraries instead
set(_ARMA_SUPPORT_LIBRARIES)

if(_ARMA_USE_WRAPPER)
  # Link to the armadillo wrapper library.
  find_library(ARMADILLO_LIBRARY
    NAMES armadillo
    NAMES_PER_DIR
    PATHS
      "$ENV{ProgramFiles}/Armadillo/lib"
      "$ENV{ProgramFiles}/Armadillo/lib64"
      "$ENV{ProgramFiles}/Armadillo"
    )
  mark_as_advanced(ARMADILLO_LIBRARY)
  set(_ARMA_REQUIRED_VARS ARMADILLO_LIBRARY)
else()
  # Link directly to individual components.
  set(ARMADILLO_LIBRARY "")
  foreach(pkg
      LAPACK
      BLAS
      ARPACK
      HDF5
      )
    if(_ARMA_USE_${pkg})
      find_package(${pkg} QUIET)
      list(APPEND _ARMA_REQUIRED_VARS "${pkg}_FOUND")
      if(${pkg}_FOUND)
        list(APPEND _ARMA_SUPPORT_LIBRARIES ${${pkg}_LIBRARIES})
      endif()
    endif()
  endforeach()
endif()

find_package_handle_standard_args(Armadillo
  REQUIRED_VARS ARMADILLO_INCLUDE_DIR ${_ARMA_REQUIRED_VARS}
  VERSION_VAR ARMADILLO_VERSION_STRING)

if (ARMADILLO_FOUND)
  set(ARMADILLO_INCLUDE_DIRS ${ARMADILLO_INCLUDE_DIR})
  set(ARMADILLO_LIBRARIES ${ARMADILLO_LIBRARY} ${_ARMA_SUPPORT_LIBRARIES})
endif ()

# Clean up internal variables
unset(_ARMA_REQUIRED_VARS)
unset(_ARMA_SUPPORT_LIBRARIES)
unset(_ARMA_USE_WRAPPER)
unset(_ARMA_USE_LAPACK)
unset(_ARMA_USE_BLAS)
unset(_ARMA_USE_ARPACK)
unset(_ARMA_USE_HDF5)
unset(_ARMA_CONFIG_CONTENTS)
unset(_ARMA_HEADER_CONTENTS)
