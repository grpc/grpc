# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindODBC
--------

.. versionadded:: 3.12

Find an Open Database Connectivity (ODBC) include directory and library.

On Windows, when building with Visual Studio, this module assumes the ODBC
library is provided by the available Windows SDK.

On Unix, this module allows to search for ODBC library provided by
unixODBC or iODBC implementations of ODBC API.
This module reads hint about location of the config program:

.. variable:: ODBC_CONFIG

  Location of odbc_config or iodbc-config program

Otherwise, this module tries to find the config program,
first from unixODBC, then from iODBC.
If no config program found, this module searches for ODBC header
and library in list of known locations.

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

.. variable:: ODBC::ODBC

  Imported target for using the ODBC library, if found.

Result variables
^^^^^^^^^^^^^^^^

.. variable:: ODBC_FOUND

  Set to true if ODBC library found, otherwise false or undefined.

.. variable:: ODBC_INCLUDE_DIRS

  Paths to include directories listed in one variable for use by ODBC client.
  May be empty on Windows, where the include directory corresponding to the
  expected Windows SDK is already available in the compilation environment.

.. variable:: ODBC_LIBRARIES

  Paths to libraries to linked against to use ODBC.
  May just a library name on Windows, where the library directory corresponding
  to the expected Windows SDK is already available in the compilation environment.

.. variable:: ODBC_CONFIG

  Path to unixODBC or iODBC config program, if found or specified.

Cache variables
^^^^^^^^^^^^^^^

For users who wish to edit and control the module behavior, this module
reads hints about search locations from the following variables:

.. variable:: ODBC_INCLUDE_DIR

  Path to ODBC include directory with ``sql.h`` header.

.. variable:: ODBC_LIBRARY

  Path to ODBC library to be linked.

These variables should not be used directly by project code.

Limitations
^^^^^^^^^^^

On Windows, this module does not search for iODBC.
On Unix, there is no way to prefer unixODBC over iODBC, or vice versa,
other than providing the config program location using the ``ODBC_CONFIG``.
This module does not allow to search for a specific ODBC driver.

#]=======================================================================]

# Define lists used internally
set(_odbc_include_paths)
set(_odbc_lib_paths)
set(_odbc_lib_names)
set(_odbc_required_libs_names)

### Try Windows Kits ##########################################################
if(WIN32)
  # List names of ODBC libraries on Windows
  if(NOT MINGW)
    set(ODBC_LIBRARY odbc32.lib)
  else()
    set(ODBC_LIBRARY libodbc32.a)
  endif()
  set(_odbc_lib_names odbc32;)

  # List additional libraries required to use ODBC library
  if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Intel")
    set(_odbc_required_libs_names odbccp32;ws2_32)
  elseif(MINGW)
    set(_odbc_required_libs_names odbccp32)
  endif()
endif()

### Try unixODBC or iODBC config program ######################################
if (UNIX)
  find_program(ODBC_CONFIG
    NAMES odbc_config iodbc-config
    DOC "Path to unixODBC or iODBC config program")
  mark_as_advanced(ODBC_CONFIG)
endif()

if (UNIX AND ODBC_CONFIG)
  # unixODBC and iODBC accept unified command line options
  execute_process(COMMAND ${ODBC_CONFIG} --cflags
    OUTPUT_VARIABLE _cflags OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${ODBC_CONFIG} --libs
    OUTPUT_VARIABLE _libs OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Collect paths of include directories from CFLAGS
  separate_arguments(_cflags NATIVE_COMMAND "${_cflags}")
  foreach(arg IN LISTS _cflags)
    if("${arg}" MATCHES "^-I(.*)$")
      list(APPEND _odbc_include_paths "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  unset(_cflags)

  # Collect paths of library names and directories from LIBS
  separate_arguments(_libs NATIVE_COMMAND "${_libs}")
  foreach(arg IN LISTS _libs)
    if("${arg}" MATCHES "^-L(.*)$")
      list(APPEND _odbc_lib_paths "${CMAKE_MATCH_1}")
    elseif("${arg}" MATCHES "^-l(.*)$")
      set(_lib_name ${CMAKE_MATCH_1})
      string(REGEX MATCH "odbc" _is_odbc ${_lib_name})
      if(_is_odbc)
        list(APPEND _odbc_lib_names ${_lib_name})
      else()
        list(APPEND _odbc_required_libs_names ${_lib_name})
      endif()
      unset(_lib_name)
    endif()
  endforeach()
  unset(_libs)
endif()

### Try unixODBC or iODBC in include/lib filesystems ##########################
if (UNIX AND NOT ODBC_CONFIG)
  # List names of both ODBC libraries, unixODBC and iODBC
  set(_odbc_lib_names odbc;iodbc;unixodbc;)
endif()

### Find include directories ##################################################
find_path(ODBC_INCLUDE_DIR
  NAMES sql.h
  PATHS ${_odbc_include_paths})

if(NOT ODBC_INCLUDE_DIR AND WIN32)
  set(ODBC_INCLUDE_DIR "")
endif()

### Find libraries ############################################################
if(NOT ODBC_LIBRARY)
  find_library(ODBC_LIBRARY
    NAMES ${_odbc_lib_names}
    PATHS ${_odbc_lib_paths}
    PATH_SUFFIXES odbc)

  foreach(_lib IN LISTS _odbc_required_libs_names)
    find_library(_lib_path
      NAMES ${_lib}
      PATHS ${_odbc_lib_paths} # system parths or collected from ODBC_CONFIG
      PATH_SUFFIXES odbc)
    if(_lib_path)
      list(APPEND _odbc_required_libs_paths ${_lib_path})
    endif()
    unset(_lib_path CACHE)
  endforeach()
endif()

# Unset internal lists as no longer used
unset(_odbc_include_paths)
unset(_odbc_lib_paths)
unset(_odbc_lib_names)
unset(_odbc_required_libs_names)

### Set result variables ######################################################
set(_odbc_required_vars ODBC_LIBRARY)
if(NOT WIN32)
  list(APPEND _odbc_required_vars ODBC_INCLUDE_DIR)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ODBC DEFAULT_MSG ${_odbc_required_vars})

unset(_odbc_required_vars)

mark_as_advanced(ODBC_LIBRARY ODBC_INCLUDE_DIR)

set(ODBC_INCLUDE_DIRS ${ODBC_INCLUDE_DIR})
list(APPEND ODBC_LIBRARIES ${ODBC_LIBRARY})
list(APPEND ODBC_LIBRARIES ${_odbc_required_libs_paths})

### Import targets ############################################################
if(ODBC_FOUND)
  if(NOT TARGET ODBC::ODBC)
    if(IS_ABSOLUTE "${ODBC_LIBRARY}")
      add_library(ODBC::ODBC UNKNOWN IMPORTED)
      set_target_properties(ODBC::ODBC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${ODBC_LIBRARY}")
    else()
      add_library(ODBC::ODBC INTERFACE IMPORTED)
      set_target_properties(ODBC::ODBC PROPERTIES
        IMPORTED_LIBNAME "${ODBC_LIBRARY}")
    endif()
    set_target_properties(ODBC::ODBC PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${ODBC_INCLUDE_DIR}")

    if(_odbc_required_libs_paths)
      set_property(TARGET ODBC::ODBC APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${_odbc_required_libs_paths}")
    endif()
  endif()
endif()

unset(_odbc_required_libs_paths)
