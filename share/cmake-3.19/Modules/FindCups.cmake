# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCups
--------

Find the Common UNIX Printing System (CUPS).

Set ``CUPS_REQUIRE_IPP_DELETE_ATTRIBUTE`` to ``TRUE`` if you need a version which
features this function (i.e. at least ``1.1.19``)

Imported targets
^^^^^^^^^^^^^^^^

This module defines :prop_tgt:`IMPORTED` target ``Cups::Cups``, if Cups has
been found.

Result variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``CUPS_FOUND``
  true if CUPS headers and libraries were found
``CUPS_INCLUDE_DIRS``
  the directory containing the Cups headers
``CUPS_LIBRARIES``
  the libraries to link against to use CUPS.
``CUPS_VERSION_STRING``
  the version of CUPS found (since CMake 2.8.8)

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``CUPS_INCLUDE_DIR``
  the directory containing the Cups headers
#]=======================================================================]

find_path(CUPS_INCLUDE_DIR cups/cups.h )

find_library(CUPS_LIBRARIES NAMES cups )

if (CUPS_INCLUDE_DIR AND CUPS_LIBRARIES AND CUPS_REQUIRE_IPP_DELETE_ATTRIBUTE)
    include(${CMAKE_CURRENT_LIST_DIR}/CheckLibraryExists.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/CMakePushCheckState.cmake)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_QUIET ${Cups_FIND_QUIETLY})

    # ippDeleteAttribute is new in cups-1.1.19 (and used by kdeprint)
    CHECK_LIBRARY_EXISTS(cups ippDeleteAttribute "" CUPS_HAS_IPP_DELETE_ATTRIBUTE)
    cmake_pop_check_state()
endif ()

if (CUPS_INCLUDE_DIR AND EXISTS "${CUPS_INCLUDE_DIR}/cups/cups.h")
    file(STRINGS "${CUPS_INCLUDE_DIR}/cups/cups.h" cups_version_str
         REGEX "^#[\t ]*define[\t ]+CUPS_VERSION_(MAJOR|MINOR|PATCH)[\t ]+[0-9]+$")

    unset(CUPS_VERSION_STRING)
    foreach(VPART MAJOR MINOR PATCH)
        foreach(VLINE ${cups_version_str})
            if(VLINE MATCHES "^#[\t ]*define[\t ]+CUPS_VERSION_${VPART}[\t ]+([0-9]+)$")
                set(CUPS_VERSION_PART "${CMAKE_MATCH_1}")
                if(CUPS_VERSION_STRING)
                    string(APPEND CUPS_VERSION_STRING ".${CUPS_VERSION_PART}")
                else()
                    set(CUPS_VERSION_STRING "${CUPS_VERSION_PART}")
                endif()
            endif()
        endforeach()
    endforeach()
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

if (CUPS_REQUIRE_IPP_DELETE_ATTRIBUTE)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(Cups
                                      REQUIRED_VARS CUPS_LIBRARIES CUPS_INCLUDE_DIR CUPS_HAS_IPP_DELETE_ATTRIBUTE
                                      VERSION_VAR CUPS_VERSION_STRING)
else ()
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(Cups
                                      REQUIRED_VARS CUPS_LIBRARIES CUPS_INCLUDE_DIR
                                      VERSION_VAR CUPS_VERSION_STRING)
endif ()

mark_as_advanced(CUPS_INCLUDE_DIR CUPS_LIBRARIES)

if (CUPS_FOUND)
    set(CUPS_INCLUDE_DIRS "${CUPS_INCLUDE_DIR}")
    if (NOT TARGET Cups::Cups)
        add_library(Cups::Cups INTERFACE IMPORTED)
        set_target_properties(Cups::Cups PROPERTIES
            INTERFACE_LINK_LIBRARIES      "${CUPS_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${CUPS_INCLUDE_DIR}")
    endif ()
endif ()
