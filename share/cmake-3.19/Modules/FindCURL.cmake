# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCURL
--------

Find the native CURL headers and libraries.

This module accept optional COMPONENTS to check supported features and
protocols::

  PROTOCOLS: ICT FILE FTP FTPS GOPHER HTTP HTTPS IMAP IMAPS LDAP LDAPS POP3
             POP3S RTMP RTSP SCP SFTP SMB SMBS SMTP SMTPS TELNET TFTP
  FEATURES:  SSL IPv6 UnixSockets libz AsynchDNS IDN GSS-API PSL SPNEGO
             Kerberos NTLM NTLM_WB TLS-SRP HTTP2 HTTPS-proxy

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines :prop_tgt:`IMPORTED` target ``CURL::libcurl``, if
curl has been found.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variables:

``CURL_FOUND``
  "True" if ``curl`` found.

``CURL_INCLUDE_DIRS``
  where to find ``curl``/``curl.h``, etc.

``CURL_LIBRARIES``
  List of libraries when using ``curl``.

``CURL_VERSION_STRING``
  The version of ``curl`` found.

CURL CMake
^^^^^^^^^^

If CURL was built using the CMake buildsystem then it provides its own
``CURLConfig.cmake`` file for use with the :command:`find_package` command's
config mode. This module looks for this file and, if found,
returns its results with no further action.

Set ``CURL_NO_CURL_CMAKE`` to ``ON`` to disable this search.

#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

if(NOT CURL_NO_CURL_CMAKE)
  # do a find package call to specifically look for the CMake version
  # of curl
  find_package(CURL QUIET NO_MODULE)
  mark_as_advanced(CURL_DIR)

  # if we found the CURL cmake package then we are done, and
  # can print what we found and return.
  if(CURL_FOUND)
    find_package_handle_standard_args(CURL HANDLE_COMPONENTS CONFIG_MODE)
    return()
  endif()
endif()

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_CURL QUIET libcurl)
  if(PC_CURL_FOUND)
    set(CURL_VERSION_STRING ${PC_CURL_VERSION})
    pkg_get_variable(CURL_SUPPORTED_PROTOCOLS libcurl supported_protocols)
    pkg_get_variable(CURL_SUPPORTED_FEATURES libcurl supported_features)
  endif()
endif()

# Look for the header file.
find_path(CURL_INCLUDE_DIR
          NAMES curl/curl.h
          HINTS ${PC_CURL_INCLUDE_DIRS})
mark_as_advanced(CURL_INCLUDE_DIR)

if(NOT CURL_LIBRARY)
  # Look for the library (sorted from most current/relevant entry to least).
  find_library(CURL_LIBRARY_RELEASE NAMES
      curl
    # Windows MSVC prebuilts:
      curllib
      libcurl_imp
      curllib_static
    # Windows older "Win32 - MSVC" prebuilts (libcurl.lib, e.g. libcurl-7.15.5-win32-msvc.zip):
      libcurl
      NAMES_PER_DIR
      HINTS ${PC_CURL_LIBRARY_DIRS}
  )
  mark_as_advanced(CURL_LIBRARY_RELEASE)

  find_library(CURL_LIBRARY_DEBUG NAMES
    # Windows MSVC CMake builds in debug configuration on vcpkg:
      libcurl-d_imp
      libcurl-d
      NAMES_PER_DIR
      HINTS ${PC_CURL_LIBRARY_DIRS}
  )
  mark_as_advanced(CURL_LIBRARY_DEBUG)

  include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
  select_library_configurations(CURL)
endif()

if(CURL_INCLUDE_DIR AND NOT CURL_VERSION_STRING)
  foreach(_curl_version_header curlver.h curl.h)
    if(EXISTS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}")
      file(STRINGS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}" curl_version_str REGEX "^#define[\t ]+LIBCURL_VERSION[\t ]+\".*\"")

      string(REGEX REPLACE "^#define[\t ]+LIBCURL_VERSION[\t ]+\"([^\"]*)\".*" "\\1" CURL_VERSION_STRING "${curl_version_str}")
      unset(curl_version_str)
      break()
    endif()
  endforeach()
endif()

if(CURL_FIND_COMPONENTS)
  set(CURL_KNOWN_PROTOCOLS ICT FILE FTP FTPS GOPHER HTTP HTTPS IMAP IMAPS LDAP LDAPS POP3 POP3S RTMP RTSP SCP SFTP SMB SMBS SMTP SMTPS TELNET TFTP)
  set(CURL_KNOWN_FEATURES  SSL IPv6 UnixSockets libz AsynchDNS IDN GSS-API PSL SPNEGO Kerberos NTLM NTLM_WB TLS-SRP HTTP2 HTTPS-proxy)
  foreach(component IN LISTS CURL_KNOWN_PROTOCOLS CURL_KNOWN_FEATURES)
    set(CURL_${component}_FOUND FALSE)
  endforeach()
  if(NOT PC_CURL_FOUND)
    find_program(CURL_CONFIG_EXECUTABLE NAMES curl-config)
    if(CURL_CONFIG_EXECUTABLE)
      execute_process(COMMAND ${CURL_CONFIG_EXECUTABLE} --version
                      OUTPUT_VARIABLE CURL_CONFIG_VERSION_STRING
                      ERROR_QUIET
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      execute_process(COMMAND ${CURL_CONFIG_EXECUTABLE} --feature
                      OUTPUT_VARIABLE CURL_CONFIG_FEATURES_STRING
                      ERROR_QUIET
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      string(REPLACE "\n" ";" CURL_SUPPORTED_FEATURES "${CURL_CONFIG_FEATURES_STRING}")
      execute_process(COMMAND ${CURL_CONFIG_EXECUTABLE} --protocols
                      OUTPUT_VARIABLE CURL_CONFIG_PROTOCOLS_STRING
                      ERROR_QUIET
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      string(REPLACE "\n" ";" CURL_SUPPORTED_PROTOCOLS "${CURL_CONFIG_PROTOCOLS_STRING}")
    endif()

  endif()
  foreach(component IN LISTS CURL_FIND_COMPONENTS)
    list(FIND CURL_KNOWN_PROTOCOLS ${component} _found)
    if(NOT _found EQUAL -1)
      list(FIND CURL_SUPPORTED_PROTOCOLS ${component} _found)
      if(NOT _found EQUAL -1)
        set(CURL_${component}_FOUND TRUE)
      elseif(CURL_FIND_REQUIRED)
        message(FATAL_ERROR "CURL: Required protocol ${component} is not found")
      endif()
    else()
      list(FIND CURL_SUPPORTED_FEATURES ${component} _found)
      if(NOT _found EQUAL -1)
        set(CURL_${component}_FOUND TRUE)
      elseif(CURL_FIND_REQUIRED)
        message(FATAL_ERROR "CURL: Required feature ${component} is not found")
      endif()
    endif()
  endforeach()
endif()

find_package_handle_standard_args(CURL
                                  REQUIRED_VARS CURL_LIBRARY CURL_INCLUDE_DIR
                                  VERSION_VAR CURL_VERSION_STRING
                                  HANDLE_COMPONENTS)

if(CURL_FOUND)
  set(CURL_LIBRARIES ${CURL_LIBRARY})
  set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})

  if(NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl UNKNOWN IMPORTED)
    set_target_properties(CURL::libcurl PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}")

    if(EXISTS "${CURL_LIBRARY}")
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${CURL_LIBRARY}")
    endif()
    if(CURL_LIBRARY_RELEASE)
      set_property(TARGET CURL::libcurl APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION_RELEASE "${CURL_LIBRARY_RELEASE}")
    endif()
    if(CURL_LIBRARY_DEBUG)
      set_property(TARGET CURL::libcurl APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION_DEBUG "${CURL_LIBRARY_DEBUG}")
    endif()
  endif()
endif()
