# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindLTTngUST
------------

.. versionadded:: 3.6

Find
`Linux Trace Toolkit Next Generation (LTTng-UST) <http://lttng.org/>`__ library.

Imported target
^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target:

``LTTng::UST``
  The LTTng-UST library, if found

Result variables
^^^^^^^^^^^^^^^^

This module sets the following

``LTTNGUST_FOUND``
  ``TRUE`` if system has LTTng-UST
``LTTNGUST_INCLUDE_DIRS``
  The LTTng-UST include directories
``LTTNGUST_LIBRARIES``
  The libraries needed to use LTTng-UST
``LTTNGUST_VERSION_STRING``
  The LTTng-UST version
``LTTNGUST_HAS_TRACEF``
  ``TRUE`` if the ``tracef()`` API is available in the system's LTTng-UST
``LTTNGUST_HAS_TRACELOG``
  ``TRUE`` if the ``tracelog()`` API is available in the system's LTTng-UST
#]=======================================================================]

find_path(LTTNGUST_INCLUDE_DIRS NAMES lttng/tracepoint.h)
find_library(LTTNGUST_LIBRARIES NAMES lttng-ust)

if(LTTNGUST_INCLUDE_DIRS AND LTTNGUST_LIBRARIES)
  # find tracef() and tracelog() support
  set(LTTNGUST_HAS_TRACEF 0)
  set(LTTNGUST_HAS_TRACELOG 0)

  if(EXISTS "${LTTNGUST_INCLUDE_DIRS}/lttng/tracef.h")
    set(LTTNGUST_HAS_TRACEF TRUE)
  endif()

  if(EXISTS "${LTTNGUST_INCLUDE_DIRS}/lttng/tracelog.h")
    set(LTTNGUST_HAS_TRACELOG TRUE)
  endif()

  # get version
  set(lttngust_version_file "${LTTNGUST_INCLUDE_DIRS}/lttng/ust-version.h")

  if(EXISTS "${lttngust_version_file}")
    file(STRINGS "${lttngust_version_file}" lttngust_version_major_string
         REGEX "^[\t ]*#define[\t ]+LTTNG_UST_MAJOR_VERSION[\t ]+[0-9]+[\t ]*$")
    file(STRINGS "${lttngust_version_file}" lttngust_version_minor_string
         REGEX "^[\t ]*#define[\t ]+LTTNG_UST_MINOR_VERSION[\t ]+[0-9]+[\t ]*$")
    file(STRINGS "${lttngust_version_file}" lttngust_version_patch_string
         REGEX "^[\t ]*#define[\t ]+LTTNG_UST_PATCHLEVEL_VERSION[\t ]+[0-9]+[\t ]*$")
    string(REGEX REPLACE ".*([0-9]+).*" "\\1"
           lttngust_v_major "${lttngust_version_major_string}")
    string(REGEX REPLACE ".*([0-9]+).*" "\\1"
           lttngust_v_minor "${lttngust_version_minor_string}")
    string(REGEX REPLACE ".*([0-9]+).*" "\\1"
           lttngust_v_patch "${lttngust_version_patch_string}")
    set(LTTNGUST_VERSION_STRING
        "${lttngust_v_major}.${lttngust_v_minor}.${lttngust_v_patch}")
    unset(lttngust_version_major_string)
    unset(lttngust_version_minor_string)
    unset(lttngust_version_patch_string)
    unset(lttngust_v_major)
    unset(lttngust_v_minor)
    unset(lttngust_v_patch)
  endif()

  unset(lttngust_version_file)

  if(NOT TARGET LTTng::UST)
    add_library(LTTng::UST UNKNOWN IMPORTED)
    set_target_properties(LTTng::UST PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LTTNGUST_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS}
      IMPORTED_LINK_INTERFACE_LANGUAGES "C"
      IMPORTED_LOCATION "${LTTNGUST_LIBRARIES}")
  endif()

  # add libdl to required libraries
  set(LTTNGUST_LIBRARIES ${LTTNGUST_LIBRARIES} ${CMAKE_DL_LIBS})
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(LTTngUST FOUND_VAR LTTNGUST_FOUND
                                  REQUIRED_VARS LTTNGUST_LIBRARIES
                                                LTTNGUST_INCLUDE_DIRS
                                  VERSION_VAR LTTNGUST_VERSION_STRING)
mark_as_advanced(LTTNGUST_LIBRARIES LTTNGUST_INCLUDE_DIRS)
