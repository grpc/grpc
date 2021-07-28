# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLibXslt
-----------

Find the XSL Transformations, Extensible Stylesheet Language
Transformations (XSLT) library (LibXslt)

IMPORTED Targets
^^^^^^^^^^^^^^^^

The following :prop_tgt:`IMPORTED` targets may be defined:

``LibXslt::LibXslt``
  If the libxslt library has been found
``LibXslt::LibExslt``
  If the libexslt library has been found
``LibXslt::xsltproc``
  If the xsltproc command-line executable has been found

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

  LIBXSLT_FOUND - system has LibXslt
  LIBXSLT_INCLUDE_DIR - the LibXslt include directory
  LIBXSLT_LIBRARIES - Link these to LibXslt
  LIBXSLT_DEFINITIONS - Compiler switches required for using LibXslt
  LIBXSLT_VERSION_STRING - version of LibXslt found (since CMake 2.8.8)

Additionally, the following two variables are set (but not required
for using xslt):

``LIBXSLT_EXSLT_INCLUDE_DIR``
  The include directory for exslt.
``LIBXSLT_EXSLT_LIBRARIES``
  Link to these if you need to link against the exslt library.
``LIBXSLT_XSLTPROC_EXECUTABLE``
  Contains the full path to the xsltproc executable if found.
#]=======================================================================]

# use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig QUIET)
PKG_CHECK_MODULES(PC_LIBXSLT QUIET libxslt)
set(LIBXSLT_DEFINITIONS ${PC_LIBXSLT_CFLAGS_OTHER})

find_path(LIBXSLT_INCLUDE_DIR NAMES libxslt/xslt.h
    HINTS
   ${PC_LIBXSLT_INCLUDEDIR}
   ${PC_LIBXSLT_INCLUDE_DIRS}
  )

# CMake 3.17 and below used 'LIBXSLT_LIBRARIES' as the name of
# the cache entry storing the find_library result.  Use the
# value if it was set by the project or user.
if(DEFINED LIBXSLT_LIBRARIES AND NOT DEFINED LIBXSLT_LIBRARY)
  set(LIBXSLT_LIBRARY ${LIBXSLT_LIBRARIES})
endif()

find_library(LIBXSLT_LIBRARY NAMES xslt libxslt
    HINTS
   ${PC_LIBXSLT_LIBDIR}
   ${PC_LIBXSLT_LIBRARY_DIRS}
  )

set(LIBXSLT_LIBRARIES ${LIBXSLT_LIBRARY})

PKG_CHECK_MODULES(PC_LIBXSLT_EXSLT QUIET libexslt)
set(LIBXSLT_EXSLT_DEFINITIONS ${PC_LIBXSLT_EXSLT_CFLAGS_OTHER})

find_path(LIBXSLT_EXSLT_INCLUDE_DIR NAMES libexslt/exslt.h
  HINTS
  ${PC_LIBXSLT_EXSLT_INCLUDEDIR}
  ${PC_LIBXSLT_EXSLT_INCLUDE_DIRS}
)

find_library(LIBXSLT_EXSLT_LIBRARY NAMES exslt libexslt
    HINTS
    ${PC_LIBXSLT_LIBDIR}
    ${PC_LIBXSLT_LIBRARY_DIRS}
    ${PC_LIBXSLT_EXSLT_LIBDIR}
    ${PC_LIBXSLT_EXSLT_LIBRARY_DIRS}
  )

set(LIBXSLT_EXSLT_LIBRARIES ${LIBXSLT_EXSLT_LIBRARY} )

find_program(LIBXSLT_XSLTPROC_EXECUTABLE xsltproc)

if(PC_LIBXSLT_VERSION)
    set(LIBXSLT_VERSION_STRING ${PC_LIBXSLT_VERSION})
elseif(LIBXSLT_INCLUDE_DIR AND EXISTS "${LIBXSLT_INCLUDE_DIR}/libxslt/xsltconfig.h")
    file(STRINGS "${LIBXSLT_INCLUDE_DIR}/libxslt/xsltconfig.h" libxslt_version_str
         REGEX "^#define[\t ]+LIBXSLT_DOTTED_VERSION[\t ]+\".*\"")

    string(REGEX REPLACE "^#define[\t ]+LIBXSLT_DOTTED_VERSION[\t ]+\"([^\"]*)\".*" "\\1"
           LIBXSLT_VERSION_STRING "${libxslt_version_str}")
    unset(libxslt_version_str)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibXslt
                                  REQUIRED_VARS LIBXSLT_LIBRARIES LIBXSLT_INCLUDE_DIR
                                  VERSION_VAR LIBXSLT_VERSION_STRING)

mark_as_advanced(LIBXSLT_INCLUDE_DIR
                 LIBXSLT_LIBRARIES
                 LIBXSLT_EXSLT_LIBRARY
                 LIBXSLT_XSLTPROC_EXECUTABLE)

if(LIBXSLT_FOUND AND NOT TARGET LibXslt::LibXslt)
  add_library(LibXslt::LibXslt UNKNOWN IMPORTED)
  set_target_properties(LibXslt::LibXslt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBXSLT_INCLUDE_DIR}")
  set_target_properties(LibXslt::LibXslt PROPERTIES INTERFACE_COMPILE_OPTIONS "${LIBXSLT_DEFINITIONS}")
  set_property(TARGET LibXslt::LibXslt APPEND PROPERTY IMPORTED_LOCATION "${LIBXSLT_LIBRARY}")
endif()

if(LIBXSLT_FOUND AND NOT TARGET LibXslt::LibExslt)
  add_library(LibXslt::LibExslt UNKNOWN IMPORTED)
  set_target_properties(LibXslt::LibExslt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBXSLT_EXSLT_INCLUDE_DIR}")
  set_target_properties(LibXslt::LibExslt PROPERTIES INTERFACE_COMPILE_OPTIONS "${LIBXSLT_EXSLT_DEFINITIONS}")
  set_property(TARGET LibXslt::LibExslt APPEND PROPERTY IMPORTED_LOCATION "${LIBXSLT_EXSLT_LIBRARY}")
endif()

if(LIBXSLT_XSLTPROC_EXECUTABLE AND NOT TARGET LibXslt::xsltproc)
  add_executable(LibXslt::xsltproc IMPORTED)
  set_target_properties(LibXslt::xsltproc PROPERTIES IMPORTED_LOCATION "${LIBXSLT_XSLTPROC_EXECUTABLE}")
endif()
