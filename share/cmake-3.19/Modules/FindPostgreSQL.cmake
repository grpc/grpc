# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPostgreSQL
--------------

Find the PostgreSQL installation.

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines :prop_tgt:`IMPORTED` target ``PostgreSQL::PostgreSQL``
if PostgreSQL has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``PostgreSQL_FOUND``
  True if PostgreSQL is found.
``PostgreSQL_LIBRARIES``
  the PostgreSQL libraries needed for linking
``PostgreSQL_INCLUDE_DIRS``
  the directories of the PostgreSQL headers
``PostgreSQL_LIBRARY_DIRS``
  the link directories for PostgreSQL libraries
``PostgreSQL_VERSION_STRING``
  the version of PostgreSQL found
#]=======================================================================]

# ----------------------------------------------------------------------------
# History:
# This module is derived from the module originally found in the VTK source tree.
#
# ----------------------------------------------------------------------------
# Note:
# PostgreSQL_ADDITIONAL_VERSIONS is a variable that can be used to set the
# version number of the implementation of PostgreSQL.
# In Windows the default installation of PostgreSQL uses that as part of the path.
# E.g C:\Program Files\PostgreSQL\8.4.
# Currently, the following version numbers are known to this module:
# "11" "10" "9.6" "9.5" "9.4" "9.3" "9.2" "9.1" "9.0" "8.4" "8.3" "8.2" "8.1" "8.0"
#
# To use this variable just do something like this:
# set(PostgreSQL_ADDITIONAL_VERSIONS "9.2" "8.4.4")
# before calling find_package(PostgreSQL) in your CMakeLists.txt file.
# This will mean that the versions you set here will be found first in the order
# specified before the default ones are searched.
#
# ----------------------------------------------------------------------------
# You may need to manually set:
#  PostgreSQL_INCLUDE_DIR  - the path to where the PostgreSQL include files are.
#  PostgreSQL_LIBRARY_DIR  - The path to where the PostgreSQL library files are.
# If FindPostgreSQL.cmake cannot find the include files or the library files.
#
# ----------------------------------------------------------------------------
# The following variables are set if PostgreSQL is found:
#  PostgreSQL_FOUND         - Set to true when PostgreSQL is found.
#  PostgreSQL_INCLUDE_DIRS  - Include directories for PostgreSQL
#  PostgreSQL_LIBRARY_DIRS  - Link directories for PostgreSQL libraries
#  PostgreSQL_LIBRARIES     - The PostgreSQL libraries.
#
# The ``PostgreSQL::PostgreSQL`` imported target is also created.
#
# ----------------------------------------------------------------------------
# If you have installed PostgreSQL in a non-standard location.
# (Please note that in the following comments, it is assumed that <Your Path>
# points to the root directory of the include directory of PostgreSQL.)
# Then you have three options.
# 1) After CMake runs, set PostgreSQL_INCLUDE_DIR to <Your Path>/include and
#    PostgreSQL_LIBRARY_DIR to wherever the library pq (or libpq in windows) is
# 2) Use CMAKE_INCLUDE_PATH to set a path to <Your Path>/PostgreSQL<-version>. This will allow find_path()
#    to locate PostgreSQL_INCLUDE_DIR by utilizing the PATH_SUFFIXES option. e.g. In your CMakeLists.txt file
#    set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "<Your Path>/include")
# 3) Set an environment variable called ${PostgreSQL_ROOT} that points to the root of where you have
#    installed PostgreSQL, e.g. <Your Path>.
#
# ----------------------------------------------------------------------------

set(PostgreSQL_INCLUDE_PATH_DESCRIPTION "top-level directory containing the PostgreSQL include directories. E.g /usr/local/include/PostgreSQL/8.4 or C:/Program Files/PostgreSQL/8.4/include")
set(PostgreSQL_INCLUDE_DIR_MESSAGE "Set the PostgreSQL_INCLUDE_DIR cmake cache entry to the ${PostgreSQL_INCLUDE_PATH_DESCRIPTION}")
set(PostgreSQL_LIBRARY_PATH_DESCRIPTION "top-level directory containing the PostgreSQL libraries.")
set(PostgreSQL_LIBRARY_DIR_MESSAGE "Set the PostgreSQL_LIBRARY_DIR cmake cache entry to the ${PostgreSQL_LIBRARY_PATH_DESCRIPTION}")
set(PostgreSQL_ROOT_DIR_MESSAGE "Set the PostgreSQL_ROOT system variable to where PostgreSQL is found on the machine E.g C:/Program Files/PostgreSQL/8.4")


set(PostgreSQL_KNOWN_VERSIONS ${PostgreSQL_ADDITIONAL_VERSIONS}
    "13" "12" "11" "10" "9.6" "9.5" "9.4" "9.3" "9.2" "9.1" "9.0" "8.4" "8.3" "8.2" "8.1" "8.0")

# Define additional search paths for root directories.
set( PostgreSQL_ROOT_DIRECTORIES
   ENV PostgreSQL_ROOT
   ${PostgreSQL_ROOT}
)
foreach(suffix ${PostgreSQL_KNOWN_VERSIONS})
  if(WIN32)
    list(APPEND PostgreSQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/lib")
    list(APPEND PostgreSQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/include")
    list(APPEND PostgreSQL_TYPE_ADDITIONAL_SEARCH_SUFFIXES
        "PostgreSQL/${suffix}/include/server")
  endif()
  if(UNIX)
    list(APPEND PostgreSQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES
        "postgresql${suffix}"
        "pgsql-${suffix}/lib")
    list(APPEND PostgreSQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES
        "postgresql${suffix}"
        "postgresql/${suffix}"
        "pgsql-${suffix}/include")
    list(APPEND PostgreSQL_TYPE_ADDITIONAL_SEARCH_SUFFIXES
        "postgresql${suffix}/server"
        "postgresql/${suffix}/server"
        "pgsql-${suffix}/include/server")
  endif()
endforeach()

#
# Look for an installation.
#
find_path(PostgreSQL_INCLUDE_DIR
  NAMES libpq-fe.h
  PATHS
   # Look in other places.
   ${PostgreSQL_ROOT_DIRECTORIES}
  PATH_SUFFIXES
    pgsql
    postgresql
    include
    ${PostgreSQL_INCLUDE_ADDITIONAL_SEARCH_SUFFIXES}
  # Help the user find it if we cannot.
  DOC "The ${PostgreSQL_INCLUDE_DIR_MESSAGE}"
)

find_path(PostgreSQL_TYPE_INCLUDE_DIR
  NAMES catalog/pg_type.h
  PATHS
   # Look in other places.
   ${PostgreSQL_ROOT_DIRECTORIES}
  PATH_SUFFIXES
    postgresql
    pgsql/server
    postgresql/server
    include/server
    ${PostgreSQL_TYPE_ADDITIONAL_SEARCH_SUFFIXES}
  # Help the user find it if we cannot.
  DOC "The ${PostgreSQL_INCLUDE_DIR_MESSAGE}"
)

# The PostgreSQL library.
set (PostgreSQL_LIBRARY_TO_FIND pq)
# Setting some more prefixes for the library
set (PostgreSQL_LIB_PREFIX "")
if ( WIN32 )
  set (PostgreSQL_LIB_PREFIX ${PostgreSQL_LIB_PREFIX} "lib")
  set (PostgreSQL_LIBRARY_TO_FIND ${PostgreSQL_LIB_PREFIX}${PostgreSQL_LIBRARY_TO_FIND})
endif()

function(__postgresql_find_library _name)
  find_library(${_name}
   NAMES ${ARGN}
   PATHS
     ${PostgreSQL_ROOT_DIRECTORIES}
   PATH_SUFFIXES
     lib
     ${PostgreSQL_LIBRARY_ADDITIONAL_SEARCH_SUFFIXES}
   # Help the user find it if we cannot.
   DOC "The ${PostgreSQL_LIBRARY_DIR_MESSAGE}"
  )
endfunction()

# For compatibility with versions prior to this multi-config search, honor
# any PostgreSQL_LIBRARY that is already specified and skip the search.
if(PostgreSQL_LIBRARY)
  set(PostgreSQL_LIBRARIES "${PostgreSQL_LIBRARY}")
  get_filename_component(PostgreSQL_LIBRARY_DIR "${PostgreSQL_LIBRARY}" PATH)
else()
  __postgresql_find_library(PostgreSQL_LIBRARY_RELEASE ${PostgreSQL_LIBRARY_TO_FIND})
  __postgresql_find_library(PostgreSQL_LIBRARY_DEBUG ${PostgreSQL_LIBRARY_TO_FIND}d)
  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(PostgreSQL)
  mark_as_advanced(PostgreSQL_LIBRARY_RELEASE PostgreSQL_LIBRARY_DEBUG)
  if(PostgreSQL_LIBRARY_RELEASE)
    get_filename_component(PostgreSQL_LIBRARY_DIR "${PostgreSQL_LIBRARY_RELEASE}" PATH)
  elseif(PostgreSQL_LIBRARY_DEBUG)
    get_filename_component(PostgreSQL_LIBRARY_DIR "${PostgreSQL_LIBRARY_DEBUG}" PATH)
  else()
    set(PostgreSQL_LIBRARY_DIR "")
  endif()
endif()

if (PostgreSQL_INCLUDE_DIR)
  # Some platforms include multiple pg_config.hs for multi-lib configurations
  # This is a temporary workaround.  A better solution would be to compile
  # a dummy c file and extract the value of the symbol.
  file(GLOB _PG_CONFIG_HEADERS "${PostgreSQL_INCLUDE_DIR}/pg_config*.h")
  foreach(_PG_CONFIG_HEADER ${_PG_CONFIG_HEADERS})
    if(EXISTS "${_PG_CONFIG_HEADER}")
      file(STRINGS "${_PG_CONFIG_HEADER}" pgsql_version_str
           REGEX "^#define[\t ]+PG_VERSION_NUM[\t ]+.*")
      if(pgsql_version_str)
        string(REGEX REPLACE "^#define[\t ]+PG_VERSION_NUM[\t ]+([0-9]*).*"
               "\\1" _PostgreSQL_VERSION_NUM "${pgsql_version_str}")
        break()
      endif()
    endif()
  endforeach()
  if (_PostgreSQL_VERSION_NUM)
    # 9.x and older encoding
    if (_PostgreSQL_VERSION_NUM LESS 100000)
      math(EXPR _PostgreSQL_major_version "${_PostgreSQL_VERSION_NUM} / 10000")
      math(EXPR _PostgreSQL_minor_version "${_PostgreSQL_VERSION_NUM} % 10000 / 100")
      math(EXPR _PostgreSQL_patch_version "${_PostgreSQL_VERSION_NUM} % 100")
      set(PostgreSQL_VERSION_STRING "${_PostgreSQL_major_version}.${_PostgreSQL_minor_version}.${_PostgreSQL_patch_version}")
      unset(_PostgreSQL_major_version)
      unset(_PostgreSQL_minor_version)
      unset(_PostgreSQL_patch_version)
    else ()
      math(EXPR _PostgreSQL_major_version "${_PostgreSQL_VERSION_NUM} / 10000")
      math(EXPR _PostgreSQL_minor_version "${_PostgreSQL_VERSION_NUM} % 10000")
      set(PostgreSQL_VERSION_STRING "${_PostgreSQL_major_version}.${_PostgreSQL_minor_version}")
      unset(_PostgreSQL_major_version)
      unset(_PostgreSQL_minor_version)
    endif ()
  else ()
    foreach(_PG_CONFIG_HEADER ${_PG_CONFIG_HEADERS})
      if(EXISTS "${_PG_CONFIG_HEADER}")
        file(STRINGS "${_PG_CONFIG_HEADER}" pgsql_version_str
             REGEX "^#define[\t ]+PG_VERSION[\t ]+\".*\"")
        if(pgsql_version_str)
          string(REGEX REPLACE "^#define[\t ]+PG_VERSION[\t ]+\"([^\"]*)\".*"
                 "\\1" PostgreSQL_VERSION_STRING "${pgsql_version_str}")
          break()
        endif()
      endif()
    endforeach()
  endif ()
  unset(_PostgreSQL_VERSION_NUM)
  unset(pgsql_version_str)
endif()

# Did we find anything?
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(PostgreSQL
                                  REQUIRED_VARS PostgreSQL_LIBRARY PostgreSQL_INCLUDE_DIR PostgreSQL_TYPE_INCLUDE_DIR
                                  VERSION_VAR PostgreSQL_VERSION_STRING)
set(PostgreSQL_FOUND  ${POSTGRESQL_FOUND})

function(__postgresql_import_library _target _var _config)
  if(_config)
    set(_config_suffix "_${_config}")
  else()
    set(_config_suffix "")
  endif()

  set(_lib "${${_var}${_config_suffix}}")
  if(EXISTS "${_lib}")
    if(_config)
      set_property(TARGET ${_target} APPEND PROPERTY
        IMPORTED_CONFIGURATIONS ${_config})
    endif()
    set_target_properties(${_target} PROPERTIES
      IMPORTED_LOCATION${_config_suffix} "${_lib}")
  endif()
endfunction()

# Now try to get the include and library path.
if(PostgreSQL_FOUND)
  if (NOT TARGET PostgreSQL::PostgreSQL)
    add_library(PostgreSQL::PostgreSQL UNKNOWN IMPORTED)
    set_target_properties(PostgreSQL::PostgreSQL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${PostgreSQL_INCLUDE_DIR};${PostgreSQL_TYPE_INCLUDE_DIR}")
    __postgresql_import_library(PostgreSQL::PostgreSQL PostgreSQL_LIBRARY "")
    __postgresql_import_library(PostgreSQL::PostgreSQL PostgreSQL_LIBRARY "RELEASE")
    __postgresql_import_library(PostgreSQL::PostgreSQL PostgreSQL_LIBRARY "DEBUG")
  endif ()
  set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_INCLUDE_DIR} ${PostgreSQL_TYPE_INCLUDE_DIR} )
  set(PostgreSQL_LIBRARY_DIRS ${PostgreSQL_LIBRARY_DIR} )
endif()

mark_as_advanced(PostgreSQL_INCLUDE_DIR PostgreSQL_TYPE_INCLUDE_DIR)
