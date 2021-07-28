# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindXalanC
-----------

.. versionadded:: 3.5

Find the Apache Xalan-C++ XSL transform processor headers and libraries.

Imported targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``XalanC::XalanC``
  The Xalan-C++ ``xalan-c`` library, if found.

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``XalanC_FOUND``
  true if the Xalan headers and libraries were found
``XalanC_VERSION``
  Xalan release version
``XalanC_INCLUDE_DIRS``
  the directory containing the Xalan headers; note
  ``XercesC_INCLUDE_DIRS`` is also required
``XalanC_LIBRARIES``
  Xalan libraries to be linked; note ``XercesC_LIBRARIES`` is also
  required

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``XalanC_INCLUDE_DIR``
  the directory containing the Xalan headers
``XalanC_LIBRARY``
  the Xalan library
#]=======================================================================]

# Written by Roger Leigh <rleigh@codelibre.net>

function(_XalanC_GET_VERSION  version_hdr)
    file(STRINGS ${version_hdr} _contents REGEX "^[ \t]*#define XALAN_VERSION_.*")
    if(_contents)
        string(REGEX REPLACE "[^*]*#define XALAN_VERSION_MAJOR[ \t(]+([0-9]+).*" "\\1" XalanC_MAJOR "${_contents}")
        string(REGEX REPLACE "[^*]*#define XALAN_VERSION_MINOR[ \t(]+([0-9]+).*" "\\1" XalanC_MINOR "${_contents}")
        string(REGEX REPLACE "[^*]*#define XALAN_VERSION_REVISION[ \t(]+([0-9]+).*" "\\1" XalanC_PATCH "${_contents}")

        if(NOT XalanC_MAJOR MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XALAN_VERSION_MAJOR!")
        endif()
        if(NOT XalanC_MINOR MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XALAN_VERSION_MINOR!")
        endif()
        if(NOT XalanC_PATCH MATCHES "^[0-9]+$")
            message(FATAL_ERROR "Version parsing failed for XALAN_VERSION_REVISION!")
        endif()

        set(XalanC_VERSION "${XalanC_MAJOR}.${XalanC_MINOR}.${XalanC_PATCH}" PARENT_SCOPE)
        set(XalanC_VERSION_MAJOR "${XalanC_MAJOR}" PARENT_SCOPE)
        set(XalanC_VERSION_MINOR "${XalanC_MINOR}" PARENT_SCOPE)
        set(XalanC_VERSION_PATCH "${XalanC_PATCH}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Include file ${version_hdr} does not exist or does not contain expected version information")
    endif()
endfunction()

# Find include directory
find_path(XalanC_INCLUDE_DIR
          NAMES "xalanc/XalanTransformer/XalanTransformer.hpp"
          DOC "Xalan-C++ include directory")
mark_as_advanced(XalanC_INCLUDE_DIR)

if(XalanC_INCLUDE_DIR AND EXISTS "${XalanC_INCLUDE_DIR}/xalanc/Include/XalanVersion.hpp")
  _XalanC_GET_VERSION("${XalanC_INCLUDE_DIR}/xalanc/Include/XalanVersion.hpp")
endif()

if(NOT XalanC_LIBRARY)
  # Find all XalanC libraries
  find_library(XalanC_LIBRARY_RELEASE
               NAMES "Xalan-C" "xalan-c"
                     "Xalan-C_${XalanC_VERSION_MAJOR}"
                     "Xalan-C_${XalanC_VERSION_MAJOR}_${XalanC_VERSION_MINOR}"
               DOC "Xalan-C++ libraries (release)")
  find_library(XalanC_LIBRARY_DEBUG
               NAMES "Xalan-CD" "xalan-cd"
                     "Xalan-C_${XalanC_VERSION_MAJOR}D"
                     "Xalan-C_${XalanC_VERSION_MAJOR}_${XalanC_VERSION_MINOR}D"
               DOC "Xalan-C++ libraries (debug)")
  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(XalanC)
  mark_as_advanced(XalanC_LIBRARY_RELEASE XalanC_LIBRARY_DEBUG)
endif()

unset(XalanC_VERSION_MAJOR)
unset(XalanC_VERSION_MINOR)
unset(XalanC_VERSION_PATCH)

unset(XalanC_XERCESC_REQUIRED)
if(XalanC_FIND_REQUIRED)
  set(XalanC_XERCESC_REQUIRED REQUIRED)
endif()
find_package(XercesC ${XalanC_XERCESC_REQUIRED})
unset(XalanC_XERCESC_REQUIRED)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(XalanC
                                  FOUND_VAR XalanC_FOUND
                                  REQUIRED_VARS XalanC_LIBRARY
                                                XalanC_INCLUDE_DIR
                                                XalanC_VERSION
                                                XercesC_FOUND
                                  VERSION_VAR XalanC_VERSION
                                  FAIL_MESSAGE "Failed to find XalanC")

if(XalanC_FOUND)
  set(XalanC_INCLUDE_DIRS "${XalanC_INCLUDE_DIR}" ${XercesC_INCLUDE_DIRS})
  set(XalanC_LIBRARIES "${XalanC_LIBRARY}" ${XercesC_LIBRARIES})

  # For header-only libraries
  if(NOT TARGET XalanC::XalanC)
    add_library(XalanC::XalanC UNKNOWN IMPORTED)
    if(XalanC_INCLUDE_DIRS)
      set_target_properties(XalanC::XalanC PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${XalanC_INCLUDE_DIRS}")
    endif()
    if(EXISTS "${XalanC_LIBRARY}")
      set_target_properties(XalanC::XalanC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        IMPORTED_LOCATION "${XalanC_LIBRARY}")
    endif()
    if(EXISTS "${XalanC_LIBRARY_RELEASE}")
      set_property(TARGET XalanC::XalanC APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(XalanC::XalanC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
        IMPORTED_LOCATION_RELEASE "${XalanC_LIBRARY_RELEASE}")
    endif()
    if(EXISTS "${XalanC_LIBRARY_DEBUG}")
      set_property(TARGET XalanC::XalanC APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(XalanC::XalanC PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
        IMPORTED_LOCATION_DEBUG "${XalanC_LIBRARY_DEBUG}")
    endif()
    set_target_properties(XalanC::XalanC PROPERTIES INTERFACE_LINK_LIBRARIES XercesC::XercesC)
  endif()
endif()
