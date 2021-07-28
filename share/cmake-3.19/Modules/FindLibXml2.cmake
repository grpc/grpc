# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLibXml2
-----------

Find the XML processing library (libxml2).

IMPORTED Targets
^^^^^^^^^^^^^^^^

The following :prop_tgt:`IMPORTED` targets may be defined:

``LibXml2::LibXml2``
  If the libxml2 library has been found
``LibXml2::xmllint``
  If the xmllint command-line executable has been found

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``LibXml2_FOUND``
  true if libxml2 headers and libraries were found
``LIBXML2_INCLUDE_DIR``
  the directory containing LibXml2 headers
``LIBXML2_INCLUDE_DIRS``
  list of the include directories needed to use LibXml2
``LIBXML2_LIBRARIES``
  LibXml2 libraries to be linked
``LIBXML2_DEFINITIONS``
  the compiler switches required for using LibXml2
``LIBXML2_XMLLINT_EXECUTABLE``
  path to the XML checking tool xmllint coming with LibXml2
``LIBXML2_VERSION_STRING``
  the version of LibXml2 found (since CMake 2.8.8)

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``LIBXML2_INCLUDE_DIR``
  the directory containing LibXml2 headers
``LIBXML2_LIBRARY``
  path to the LibXml2 library
#]=======================================================================]

# use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig QUIET)
PKG_CHECK_MODULES(PC_LIBXML QUIET libxml-2.0)

find_path(LIBXML2_INCLUDE_DIR NAMES libxml/xpath.h
   HINTS
   ${PC_LIBXML_INCLUDEDIR}
   ${PC_LIBXML_INCLUDE_DIRS}
   PATH_SUFFIXES libxml2
   )

# CMake 3.9 and below used 'LIBXML2_LIBRARIES' as the name of
# the cache entry storing the find_library result.  Use the
# value if it was set by the project or user.
if(DEFINED LIBXML2_LIBRARIES AND NOT DEFINED LIBXML2_LIBRARY)
  set(LIBXML2_LIBRARY ${LIBXML2_LIBRARIES})
endif()

find_library(LIBXML2_LIBRARY NAMES xml2 libxml2 libxml2_a
   HINTS
   ${PC_LIBXML_LIBDIR}
   ${PC_LIBXML_LIBRARY_DIRS}
   )

find_program(LIBXML2_XMLLINT_EXECUTABLE xmllint)
# for backwards compat. with KDE 4.0.x:
set(XMLLINT_EXECUTABLE "${LIBXML2_XMLLINT_EXECUTABLE}")

if(LIBXML2_INCLUDE_DIR AND EXISTS "${LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h")
    file(STRINGS "${LIBXML2_INCLUDE_DIR}/libxml/xmlversion.h" libxml2_version_str
         REGEX "^#define[\t ]+LIBXML_DOTTED_VERSION[\t ]+\".*\"")

    string(REGEX REPLACE "^#define[\t ]+LIBXML_DOTTED_VERSION[\t ]+\"([^\"]*)\".*" "\\1"
           LIBXML2_VERSION_STRING "${libxml2_version_str}")
    unset(libxml2_version_str)
endif()

set(LIBXML2_INCLUDE_DIRS ${LIBXML2_INCLUDE_DIR})
set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY})

# Did we find the same installation as pkg-config?
# If so, use additional information from it.
unset(LIBXML2_DEFINITIONS)
foreach(libxml2_pc_lib_dir IN LISTS PC_LIBXML_LIBDIR PC_LIBXML_LIBRARY_DIRS)
  if (LIBXML2_LIBRARY MATCHES "^${libxml2_pc_lib_dir}")
    list(APPEND LIBXML2_INCLUDE_DIRS ${PC_LIBXML_INCLUDE_DIRS})
    set(LIBXML2_DEFINITIONS ${PC_LIBXML_CFLAGS_OTHER})
    break()
  endif()
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibXml2
                                  REQUIRED_VARS LIBXML2_LIBRARY LIBXML2_INCLUDE_DIR
                                  VERSION_VAR LIBXML2_VERSION_STRING)

mark_as_advanced(LIBXML2_INCLUDE_DIR LIBXML2_LIBRARY LIBXML2_XMLLINT_EXECUTABLE)

if(LibXml2_FOUND AND NOT TARGET LibXml2::LibXml2)
  add_library(LibXml2::LibXml2 UNKNOWN IMPORTED)
  set_target_properties(LibXml2::LibXml2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${LIBXML2_INCLUDE_DIRS}")
  set_target_properties(LibXml2::LibXml2 PROPERTIES INTERFACE_COMPILE_OPTIONS "${LIBXML2_DEFINITIONS}")
  set_property(TARGET LibXml2::LibXml2 APPEND PROPERTY IMPORTED_LOCATION "${LIBXML2_LIBRARY}")
endif()

if(LIBXML2_XMLLINT_EXECUTABLE AND NOT TARGET LibXml2::xmllint)
  add_executable(LibXml2::xmllint IMPORTED)
  set_target_properties(LibXml2::xmllint PROPERTIES IMPORTED_LOCATION "${LIBXML2_XMLLINT_EXECUTABLE}")
endif()
