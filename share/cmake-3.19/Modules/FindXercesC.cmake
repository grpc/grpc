# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindXercesC
-----------

.. versionadded:: 3.1

Find the Apache Xerces-C++ validating XML parser headers and libraries.

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``XercesC::XercesC``
  The Xerces-C++ ``xerces-c`` library, if found.

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``XercesC_FOUND``
  true if the Xerces headers and libraries were found
``XercesC_VERSION``
  Xerces release version
``XercesC_INCLUDE_DIRS``
  the directory containing the Xerces headers
``XercesC_LIBRARIES``
  Xerces libraries to be linked

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``XercesC_INCLUDE_DIR``
  the directory containing the Xerces headers
``XercesC_LIBRARY``
  the Xerces library
#]=======================================================================]

# Written by Roger Leigh <rleigh@codelibre.net>

function(_XercesC_GET_VERSION  version_hdr)
    file(STRINGS ${version_hdr} _contents REGEX "^[ \t]*#define XERCES_VERSION_.*")
    if(_contents)
        string(REGEX REPLACE ".*#define XERCES_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" XercesC_MAJOR "${_contents}")
        string(REGEX REPLACE ".*#define XERCES_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" XercesC_MINOR "${_contents}")
        string(REGEX REPLACE ".*#define XERCES_VERSION_REVISION[ \t]+([0-9]+).*" "\\1" XercesC_PATCH "${_contents}")

        if(NOT XercesC_MAJOR MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XERCES_VERSION_MAJOR!")
        endif()
        if(NOT XercesC_MINOR MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XERCES_VERSION_MINOR!")
        endif()
        if(NOT XercesC_PATCH MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XERCES_VERSION_REVISION!")
        endif()

        set(XercesC_VERSION "${XercesC_MAJOR}.${XercesC_MINOR}.${XercesC_PATCH}" PARENT_SCOPE)
        set(XercesC_VERSION_MAJOR "${XercesC_MAJOR}" PARENT_SCOPE)
        set(XercesC_VERSION_MINOR "${XercesC_MINOR}" PARENT_SCOPE)
        set(XercesC_VERSION_PATCH "${XercesC_PATCH}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Include file ${version_hdr} does not exist or does not contain expected version information")
    endif()
endfunction()

# Find include directory
find_path(XercesC_INCLUDE_DIR
          NAMES "xercesc/util/PlatformUtils.hpp"
          DOC "Xerces-C++ include directory")
mark_as_advanced(XercesC_INCLUDE_DIR)

if(XercesC_INCLUDE_DIR AND EXISTS "${XercesC_INCLUDE_DIR}/xercesc/util/XercesVersion.hpp")
  _XercesC_GET_VERSION("${XercesC_INCLUDE_DIR}/xercesc/util/XercesVersion.hpp")
endif()

if(NOT XercesC_LIBRARY)
  # Find all XercesC libraries
  find_library(XercesC_LIBRARY_RELEASE
               NAMES "xerces-c"
                     "xerces-c_${XercesC_VERSION_MAJOR}"
                     "xerces-c-${XercesC_VERSION_MAJOR}.${XercesC_VERSION_MINOR}"
               DOC "Xerces-C++ libraries (release)")
  find_library(XercesC_LIBRARY_DEBUG
               NAMES "xerces-cd"
                     "xerces-c_${XercesC_VERSION_MAJOR}D"
                     "xerces-c_${XercesC_VERSION_MAJOR}_${XercesC_VERSION_MINOR}D"
               DOC "Xerces-C++ libraries (debug)")
  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(XercesC)
  mark_as_advanced(XercesC_LIBRARY_RELEASE XercesC_LIBRARY_DEBUG)
endif()

unset(XercesC_VERSION_MAJOR)
unset(XercesC_VERSION_MINOR)
unset(XercesC_VERSION_PATCH)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(XercesC
                                  FOUND_VAR XercesC_FOUND
                                  REQUIRED_VARS XercesC_LIBRARY
                                                XercesC_INCLUDE_DIR
                                                XercesC_VERSION
                                  VERSION_VAR XercesC_VERSION
                                  FAIL_MESSAGE "Failed to find XercesC")

if(XercesC_FOUND)
  set(XercesC_INCLUDE_DIRS "${XercesC_INCLUDE_DIR}")
  set(XercesC_LIBRARIES "${XercesC_LIBRARY}")

  # For header-only libraries
  if(NOT TARGET XercesC::XercesC)
    add_library(XercesC::XercesC UNKNOWN IMPORTED)
    if(XercesC_INCLUDE_DIRS)
      set_target_properties(XercesC::XercesC PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${XercesC_INCLUDE_DIRS}")
    endif()
    if(EXISTS "${XercesC_LIBRARY}")
      set_target_properties(XercesC::XercesC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        IMPORTED_LOCATION "${XercesC_LIBRARY}")
    endif()
    if(EXISTS "${XercesC_LIBRARY_RELEASE}")
      set_property(TARGET XercesC::XercesC APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(XercesC::XercesC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
        IMPORTED_LOCATION_RELEASE "${XercesC_LIBRARY_RELEASE}")
    endif()
    if(EXISTS "${XercesC_LIBRARY_DEBUG}")
      set_property(TARGET XercesC::XercesC APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(XercesC::XercesC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
        IMPORTED_LOCATION_DEBUG "${XercesC_LIBRARY_DEBUG}")
    endif()
  endif()
endif()
