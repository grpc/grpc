# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindHg
------

Extract information from a mercurial working copy.

The module defines the following variables:

::

   HG_EXECUTABLE - path to mercurial command line client (hg)
   HG_FOUND - true if the command line client was found
   HG_VERSION_STRING - the version of mercurial found

If the command line client executable is found the following macro is defined:

::

  HG_WC_INFO(<dir> <var-prefix>)

Hg_WC_INFO extracts information of a mercurial working copy
at a given location.  This macro defines the following variables:

::

  <var-prefix>_WC_CHANGESET - current changeset
  <var-prefix>_WC_REVISION - current revision

Example usage:

::

   find_package(Hg)
   if(HG_FOUND)
     message("hg found: ${HG_EXECUTABLE}")
     HG_WC_INFO(${PROJECT_SOURCE_DIR} Project)
     message("Current revision is ${Project_WC_REVISION}")
     message("Current changeset is ${Project_WC_CHANGESET}")
   endif()
#]=======================================================================]

find_program(HG_EXECUTABLE
  NAMES hg
  PATHS
    [HKEY_LOCAL_MACHINE\\Software\\TortoiseHG]
  PATH_SUFFIXES Mercurial
  DOC "hg command line client"
  )
mark_as_advanced(HG_EXECUTABLE)

if(HG_EXECUTABLE)
  set(_saved_lc_all "$ENV{LC_ALL}")
  set(ENV{LC_ALL} "C")

  set(_saved_language "$ENV{LANGUAGE}")
  set(ENV{LANGUAGE})

  execute_process(COMMAND ${HG_EXECUTABLE} --version
                  OUTPUT_VARIABLE hg_version
                  ERROR_QUIET
                  RESULT_VARIABLE hg_result
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(ENV{LC_ALL} ${_saved_lc_all})
  set(ENV{LANGUAGE} ${_saved_language})

  if(hg_result MATCHES "is not a valid Win32 application")
    set_property(CACHE HG_EXECUTABLE PROPERTY VALUE "HG_EXECUTABLE-NOTFOUND")
  endif()
  if(hg_version MATCHES "^Mercurial Distributed SCM \\(version ([0-9][^)]*)\\)")
    set(HG_VERSION_STRING "${CMAKE_MATCH_1}")
  endif()
  unset(hg_version)

  macro(HG_WC_INFO dir prefix)
    execute_process(COMMAND ${HG_EXECUTABLE} id -i -n
      WORKING_DIRECTORY ${dir}
      RESULT_VARIABLE hg_id_result
      ERROR_VARIABLE hg_id_error
      OUTPUT_VARIABLE ${prefix}_WC_DATA
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT ${hg_id_result} EQUAL 0)
      message(SEND_ERROR "Command \"${HG_EXECUTBALE} id -n\" in directory ${dir} failed with output:\n${hg_id_error}")
    endif()

    string(REGEX REPLACE "([0-9a-f]+)\\+? [0-9]+\\+?" "\\1" ${prefix}_WC_CHANGESET ${${prefix}_WC_DATA})
    string(REGEX REPLACE "[0-9a-f]+\\+? ([0-9]+)\\+?" "\\1" ${prefix}_WC_REVISION ${${prefix}_WC_DATA})
  endmacro(HG_WC_INFO)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(Hg
                                  REQUIRED_VARS HG_EXECUTABLE
                                  VERSION_VAR HG_VERSION_STRING)
