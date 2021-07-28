# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPerl
--------

Find perl

this module looks for Perl

::

  PERL_EXECUTABLE     - the full path to perl
  PERL_FOUND          - If false, don't attempt to use perl.
  PERL_VERSION_STRING - version of perl found (since CMake 2.8.8)
#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/FindCygwin.cmake)

set(PERL_POSSIBLE_BIN_PATHS
  ${CYGWIN_INSTALL_PATH}/bin
  )

if(WIN32)
  get_filename_component(
    ActivePerl_CurrentVersion
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ActiveState\\ActivePerl;CurrentVersion]"
    NAME)
  set(PERL_POSSIBLE_BIN_PATHS ${PERL_POSSIBLE_BIN_PATHS}
    "C:/Perl/bin"
    "C:/Strawberry/perl/bin"
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\ActiveState\\ActivePerl\\${ActivePerl_CurrentVersion}]/bin
    )
endif()

find_program(PERL_EXECUTABLE
  NAMES perl
  PATHS ${PERL_POSSIBLE_BIN_PATHS}
  )

if(PERL_EXECUTABLE)
  ### PERL_VERSION
  execute_process(
    COMMAND
      ${PERL_EXECUTABLE} -V:version
      OUTPUT_VARIABLE
        PERL_VERSION_OUTPUT_VARIABLE
      RESULT_VARIABLE
        PERL_VERSION_RESULT_VARIABLE
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT PERL_VERSION_RESULT_VARIABLE AND NOT PERL_VERSION_OUTPUT_VARIABLE MATCHES "^version='UNKNOWN'")
    string(REGEX REPLACE "version='([^']+)'.*" "\\1" PERL_VERSION_STRING ${PERL_VERSION_OUTPUT_VARIABLE})
  else()
    execute_process(
      COMMAND ${PERL_EXECUTABLE} -v
      OUTPUT_VARIABLE PERL_VERSION_OUTPUT_VARIABLE
      RESULT_VARIABLE PERL_VERSION_RESULT_VARIABLE
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT PERL_VERSION_RESULT_VARIABLE AND PERL_VERSION_OUTPUT_VARIABLE MATCHES "This is perl.*[ \\(]v([0-9\\._]+)[ \\)]")
      set(PERL_VERSION_STRING "${CMAKE_MATCH_1}")
    elseif(NOT PERL_VERSION_RESULT_VARIABLE AND PERL_VERSION_OUTPUT_VARIABLE MATCHES "This is perl, version ([0-9\\._]+) +")
      set(PERL_VERSION_STRING "${CMAKE_MATCH_1}")
    endif()
  endif()
endif()

# Deprecated settings for compatibility with CMake1.4
set(PERL ${PERL_EXECUTABLE})

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
if (CMAKE_FIND_PACKAGE_NAME STREQUAL "PerlLibs")
  # FindPerlLibs include()'s this module. It's an old pattern, but rather than
  # trying to suppress this from outside the module (which is then sensitive to
  # the contents, detect the case in this module and suppress it explicitly.
  set(FPHSA_NAME_MISMATCHED 1)
endif ()
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Perl
                                  REQUIRED_VARS PERL_EXECUTABLE
                                  VERSION_VAR PERL_VERSION_STRING)
unset(FPHSA_NAME_MISMATCHED)

mark_as_advanced(PERL_EXECUTABLE)
