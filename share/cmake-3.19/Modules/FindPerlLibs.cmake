# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindPerlLibs
------------

Find Perl libraries

This module finds if PERL is installed and determines where the
include files and libraries are.  It also determines what the name of
the library is.  This code sets the following variables:

::

  PERLLIBS_FOUND    = True if perl.h & libperl were found
  PERL_INCLUDE_PATH = path to where perl.h is found
  PERL_LIBRARY      = path to libperl
  PERL_EXECUTABLE   = full path to the perl binary



The minimum required version of Perl can be specified using the
standard syntax, e.g.  find_package(PerlLibs 6.0)

::

  The following variables are also available if needed
  (introduced after CMake 2.6.4)



::

  PERL_SITESEARCH     = path to the sitesearch install dir (-V:installsitesearch)
  PERL_SITEARCH       = path to the sitelib install directory (-V:installsitearch)
  PERL_SITELIB        = path to the sitelib install directory (-V:installsitelib)
  PERL_VENDORARCH     = path to the vendor arch install directory (-V:installvendorarch)
  PERL_VENDORLIB      = path to the vendor lib install directory (-V:installvendorlib)
  PERL_ARCHLIB        = path to the core arch lib install directory (-V:archlib)
  PERL_PRIVLIB        = path to the core priv lib install directory (-V:privlib)
  PERL_UPDATE_ARCHLIB = path to the update arch lib install directory (-V:installarchlib)
  PERL_UPDATE_PRIVLIB = path to the update priv lib install directory (-V:installprivlib)
  PERL_EXTRA_C_FLAGS = Compilation flags used to build perl
#]=======================================================================]

# find the perl executable
include(${CMAKE_CURRENT_LIST_DIR}/FindPerl.cmake)

if (PERL_EXECUTABLE)

  function (perl_get_info _pgi_info tag)
    cmake_parse_arguments(_PGI "IS_PATH" "" "" ${ARGN})

    set (${_pgi_info} NOTFOUND PARENT_SCOPE)

    execute_process(COMMAND "${PERL_EXECUTABLE}" -V:${tag}
      OUTPUT_VARIABLE result
      RESULT_VARIABLE status)

    if (NOT status)
      string(REGEX REPLACE "${tag}='([^']*)'.*" "\\1" result "${result}")
      if (_PGI_IS_PATH)
        file(TO_CMAKE_PATH "${result}" result)
      endif()
      set (${_pgi_info} "${result}" PARENT_SCOPE)
    endif ()
  endfunction()

  ### PERL_PREFIX
  perl_get_info(PERL_PREFIX prefix IS_PATH)

  ### PERL_ARCHNAME
  perl_get_info(PERL_ARCHNAME archname)

  ### PERL_EXTRA_C_FLAGS
  perl_get_info(PERL_EXTRA_C_FLAGS cppflags)

  ### PERL_SITESEARCH
  perl_get_info(PERL_SITESEARCH installsitesearch IS_PATH)

  ### PERL_SITEARCH
  perl_get_info(PERL_SITEARCH installsitearch IS_PATH)

  ### PERL_SITELIB
  perl_get_info(PERL_SITELIB installsitelib IS_PATH)

  ### PERL_VENDORARCH
  perl_get_info(PERL_VENDORARCH installvendorarch IS_PATH)

  ### PERL_VENDORLIB
  perl_get_info(PERL_VENDORLIB installvendorlib IS_PATH)

  ### PERL_ARCHLIB
  perl_get_info(PERL_ARCHLIB archlib IS_PATH)

  ### PERL_PRIVLIB
  perl_get_info(PERL_PRIVLIB privlib IS_PATH)

  ### PERL_UPDATE_ARCHLIB
  perl_get_info(PERL_UPDATE_ARCHLIB installarchlib IS_PATH)

  ### PERL_UPDATE_PRIVLIB
  perl_get_info(PERL_UPDATE_PRIVLIB installprivlib IS_PATH)

  ### PERL_POSSIBLE_LIBRARY_NAMES
  perl_get_info(PERL_POSSIBLE_LIBRARY_NAMES libperl)
  if (NOT PERL_POSSIBLE_LIBRARY_NAMES)
    set(PERL_POSSIBLE_LIBRARY_NAMES perl${PERL_VERSION_STRING} perl)
  endif()
  if(CMAKE_SYSTEM_NAME MATCHES "CYGWIN")
    list (APPEND PERL_POSSIBLE_LIBRARY_NAMES perl${PERL_VERSION_STRING})
  endif()
  if (CMAKE_SYSTEM_NAME MATCHES "MSYS|CYGWIN")
    # on MSYS and CYGWIN environments, current perl -V:libperl gives shared library name
    # rather than the import library. So, extends possible library names
    list (APPEND PERL_POSSIBLE_LIBRARY_NAMES perl)
  endif()

  ### PERL_INCLUDE_PATH
  find_path(PERL_INCLUDE_PATH
    NAMES
      perl.h
    PATHS
      "${PERL_UPDATE_ARCHLIB}/CORE"
      "${PERL_ARCHLIB}/CORE"
      /usr/lib/perl5/${PERL_VERSION_STRING}/${PERL_ARCHNAME}/CORE
      /usr/lib/perl/${PERL_VERSION_STRING}/${PERL_ARCHNAME}/CORE
      /usr/lib/perl5/${PERL_VERSION_STRING}/CORE
      /usr/lib/perl/${PERL_VERSION_STRING}/CORE
  )

  ### PERL_LIBRARY
  find_library(PERL_LIBRARY
    NAMES
      ${PERL_POSSIBLE_LIBRARY_NAMES}
    PATHS
      "${PERL_UPDATE_ARCHLIB}/CORE"
      "${PERL_ARCHLIB}/CORE"
      /usr/lib/perl5/${PERL_VERSION_STRING}/${PERL_ARCHNAME}/CORE
      /usr/lib/perl/${PERL_VERSION_STRING}/${PERL_ARCHNAME}/CORE
      /usr/lib/perl5/${PERL_VERSION_STRING}/CORE
      /usr/lib/perl/${PERL_VERSION_STRING}/CORE
  )

endif ()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(PerlLibs REQUIRED_VARS PERL_LIBRARY PERL_INCLUDE_PATH
                                           VERSION_VAR PERL_VERSION_STRING)

# Introduced after CMake 2.6.4 to bring module into compliance
set(PERL_INCLUDE_DIR  ${PERL_INCLUDE_PATH})
set(PERL_INCLUDE_DIRS ${PERL_INCLUDE_PATH})
set(PERL_LIBRARIES    ${PERL_LIBRARY})
# For backward compatibility with CMake before 2.8.8
set(PERL_VERSION ${PERL_VERSION_STRING})

mark_as_advanced(
  PERL_INCLUDE_PATH
  PERL_LIBRARY
)
