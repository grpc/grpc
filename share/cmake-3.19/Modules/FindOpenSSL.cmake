# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindOpenSSL
-----------

Find the OpenSSL encryption library.

Optional COMPONENTS
^^^^^^^^^^^^^^^^^^^

This module supports two optional COMPONENTS: ``Crypto`` and ``SSL``.  Both
components have associated imported targets, as described below.

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` targets:

``OpenSSL::SSL``
  The OpenSSL ``ssl`` library, if found.
``OpenSSL::Crypto``
  The OpenSSL ``crypto`` library, if found.
``OpenSSL::applink``
  The OpenSSL ``applink`` components that might be need to be compiled into
  projects under MSVC. This target is available only if found OpenSSL version
  is not less than 0.9.8. By linking this target the above OpenSSL targets can
  be linked even if the project has different MSVC runtime configurations with
  the above OpenSSL targets. This target has no effect on platforms other than
  MSVC.

NOTE: Due to how ``INTERFACE_SOURCES`` are consumed by the consuming target,
unless you certainly know what you are doing, it is always preferred to link
``OpenSSL::applink`` target as ``PRIVATE`` and to make sure that this target is
linked at most once for the whole dependency graph of any library or
executable:

.. code-block:: cmake

   target_link_libraries(myTarget PRIVATE OpenSSL::applink)

Otherwise you would probably encounter unexpected random problems when building
and linking, as both the ISO C and the ISO C++ standard claims almost nothing
about what a link process should be.

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``OPENSSL_FOUND``
  System has the OpenSSL library. If no components are requested it only
  requires the crypto library.
``OPENSSL_INCLUDE_DIR``
  The OpenSSL include directory.
``OPENSSL_CRYPTO_LIBRARY``
  The OpenSSL crypto library.
``OPENSSL_CRYPTO_LIBRARIES``
  The OpenSSL crypto library and its dependencies.
``OPENSSL_SSL_LIBRARY``
  The OpenSSL SSL library.
``OPENSSL_SSL_LIBRARIES``
  The OpenSSL SSL library and its dependencies.
``OPENSSL_LIBRARIES``
  All OpenSSL libraries and their dependencies.
``OPENSSL_VERSION``
  This is set to ``$major.$minor.$revision$patch`` (e.g. ``0.9.8s``).
``OPENSSL_APPLINK_SOURCE``
  The sources in the target ``OpenSSL::applink`` that is mentioned above. This
  variable shall always be undefined if found openssl version is less than
  0.9.8 or if platform is not MSVC.

Hints
^^^^^

Set ``OPENSSL_ROOT_DIR`` to the root directory of an OpenSSL installation.
Set ``OPENSSL_USE_STATIC_LIBS`` to ``TRUE`` to look for static libraries.
Set ``OPENSSL_MSVC_STATIC_RT`` set ``TRUE`` to choose the MT version of the lib.
#]=======================================================================]

macro(_OpenSSL_test_and_find_dependencies ssl_library crypto_library)
  if((CMAKE_SYSTEM_NAME STREQUAL "Linux") AND
     (("${ssl_library}" MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$") OR
      ("${crypto_library}" MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")))
    set(_OpenSSL_has_dependencies TRUE)
    find_package(Threads)
  else()
    set(_OpenSSL_has_dependencies FALSE)
  endif()
endmacro()

function(_OpenSSL_add_dependencies libraries_var)
  if(CMAKE_THREAD_LIBS_INIT)
    list(APPEND ${libraries_var} ${CMAKE_THREAD_LIBS_INIT})
  endif()
  list(APPEND ${libraries_var} ${CMAKE_DL_LIBS})
  set(${libraries_var} ${${libraries_var}} PARENT_SCOPE)
endfunction()

function(_OpenSSL_target_add_dependencies target)
  if(_OpenSSL_has_dependencies)
    set_property( TARGET ${target} APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads )
    set_property( TARGET ${target} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS} )
  endif()
endfunction()

if (UNIX)
  find_package(PkgConfig QUIET)
  pkg_check_modules(_OPENSSL QUIET openssl)
endif ()

# Support preference of static libs by adjusting CMAKE_FIND_LIBRARY_SUFFIXES
if(OPENSSL_USE_STATIC_LIBS)
  set(_openssl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a )
  endif()
endif()

if (WIN32)
  # http://www.slproweb.com/products/Win32OpenSSL.html
  set(_OPENSSL_ROOT_HINTS
    ${OPENSSL_ROOT_DIR}
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenSSL (32-bit)_is1;Inno Setup: App Path]"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenSSL (64-bit)_is1;Inno Setup: App Path]"
    ENV OPENSSL_ROOT_DIR
    )
  file(TO_CMAKE_PATH "$ENV{PROGRAMFILES}" _programfiles)
  set(_OPENSSL_ROOT_PATHS
    "${_programfiles}/OpenSSL"
    "${_programfiles}/OpenSSL-Win32"
    "${_programfiles}/OpenSSL-Win64"
    "C:/OpenSSL/"
    "C:/OpenSSL-Win32/"
    "C:/OpenSSL-Win64/"
    )
  unset(_programfiles)
else ()
  set(_OPENSSL_ROOT_HINTS
    ${OPENSSL_ROOT_DIR}
    ENV OPENSSL_ROOT_DIR
    )
endif ()

set(_OPENSSL_ROOT_HINTS_AND_PATHS
    HINTS ${_OPENSSL_ROOT_HINTS}
    PATHS ${_OPENSSL_ROOT_PATHS}
    )

find_path(OPENSSL_INCLUDE_DIR
  NAMES
    openssl/ssl.h
  ${_OPENSSL_ROOT_HINTS_AND_PATHS}
  HINTS
    ${_OPENSSL_INCLUDEDIR}
    ${_OPENSSL_INCLUDE_DIRS}
  PATH_SUFFIXES
    include
)

if(WIN32 AND NOT CYGWIN)
  if(MSVC)
    # /MD and /MDd are the standard values - if someone wants to use
    # others, the libnames have to change here too
    # use also ssl and ssleay32 in debug as fallback for openssl < 0.9.8b
    # enable OPENSSL_MSVC_STATIC_RT to get the libs build /MT (Multithreaded no-DLL)
    # In Visual C++ naming convention each of these four kinds of Windows libraries has it's standard suffix:
    #   * MD for dynamic-release
    #   * MDd for dynamic-debug
    #   * MT for static-release
    #   * MTd for static-debug

    # Implementation details:
    # We are using the libraries located in the VC subdir instead of the parent directory even though :
    # libeay32MD.lib is identical to ../libeay32.lib, and
    # ssleay32MD.lib is identical to ../ssleay32.lib
    # enable OPENSSL_USE_STATIC_LIBS to use the static libs located in lib/VC/static

    if (OPENSSL_MSVC_STATIC_RT)
      set(_OPENSSL_MSVC_RT_MODE "MT")
    else ()
      set(_OPENSSL_MSVC_RT_MODE "MD")
    endif ()

    # Since OpenSSL 1.1, lib names are like libcrypto32MTd.lib and libssl32MTd.lib
    if( "${CMAKE_SIZEOF_VOID_P}" STREQUAL "8" )
        set(_OPENSSL_MSVC_ARCH_SUFFIX "64")
    else()
        set(_OPENSSL_MSVC_ARCH_SUFFIX "32")
    endif()

    if(OPENSSL_USE_STATIC_LIBS)
      set(_OPENSSL_STATIC_SUFFIX
        "_static"
      )
      set(_OPENSSL_PATH_SUFFIXES
        "lib/VC/static"
        "VC/static"
        "lib"
        )
    else()
      set(_OPENSSL_STATIC_SUFFIX
        ""
      )
      set(_OPENSSL_PATH_SUFFIXES
        "lib/VC"
        "VC"
        "lib"
        )
    endif ()

    find_library(LIB_EAY_DEBUG
      NAMES
        # When OpenSSL is built with default options, the static library name is suffixed with "_static".
        # Looking the "libcrypto_static.lib" with a higher priority than "libcrypto.lib" which is the
        # import library of "libcrypto.dll".
        libcrypto${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libcrypto${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libcrypto${_OPENSSL_STATIC_SUFFIX}d
        libeay32${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libeay32${_OPENSSL_STATIC_SUFFIX}d
        crypto${_OPENSSL_STATIC_SUFFIX}d
        # When OpenSSL is built with the "-static" option, only the static build is produced,
        # and it is not suffixed with "_static".
        libcrypto${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libcrypto${_OPENSSL_MSVC_RT_MODE}d
        libcryptod
        libeay32${_OPENSSL_MSVC_RT_MODE}d
        libeay32d
        cryptod
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        ${_OPENSSL_PATH_SUFFIXES}
    )

    find_library(LIB_EAY_RELEASE
      NAMES
        # When OpenSSL is built with default options, the static library name is suffixed with "_static".
        # Looking the "libcrypto_static.lib" with a higher priority than "libcrypto.lib" which is the
        # import library of "libcrypto.dll".
        libcrypto${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libcrypto${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libcrypto${_OPENSSL_STATIC_SUFFIX}
        libeay32${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libeay32${_OPENSSL_STATIC_SUFFIX}
        crypto${_OPENSSL_STATIC_SUFFIX}
        # When OpenSSL is built with the "-static" option, only the static build is produced,
        # and it is not suffixed with "_static".
        libcrypto${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libcrypto${_OPENSSL_MSVC_RT_MODE}
        libcrypto
        libeay32${_OPENSSL_MSVC_RT_MODE}
        libeay32
        crypto
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        ${_OPENSSL_PATH_SUFFIXES}
    )

    find_library(SSL_EAY_DEBUG
      NAMES
        # When OpenSSL is built with default options, the static library name is suffixed with "_static".
        # Looking the "libssl_static.lib" with a higher priority than "libssl.lib" which is the
        # import library of "libssl.dll".
        libssl${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libssl${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libssl${_OPENSSL_STATIC_SUFFIX}d
        ssleay32${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        ssleay32${_OPENSSL_STATIC_SUFFIX}d
        ssl${_OPENSSL_STATIC_SUFFIX}d
        # When OpenSSL is built with the "-static" option, only the static build is produced,
        # and it is not suffixed with "_static".
        libssl${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}d
        libssl${_OPENSSL_MSVC_RT_MODE}d
        libssld
        ssleay32${_OPENSSL_MSVC_RT_MODE}d
        ssleay32d
        ssld
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        ${_OPENSSL_PATH_SUFFIXES}
    )

    find_library(SSL_EAY_RELEASE
      NAMES
        # When OpenSSL is built with default options, the static library name is suffixed with "_static".
        # Looking the "libssl_static.lib" with a higher priority than "libssl.lib" which is the
        # import library of "libssl.dll".
        libssl${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libssl${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libssl${_OPENSSL_STATIC_SUFFIX}
        ssleay32${_OPENSSL_STATIC_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        ssleay32${_OPENSSL_STATIC_SUFFIX}
        ssl${_OPENSSL_STATIC_SUFFIX}
        # When OpenSSL is built with the "-static" option, only the static build is produced,
        # and it is not suffixed with "_static".
        libssl${_OPENSSL_MSVC_ARCH_SUFFIX}${_OPENSSL_MSVC_RT_MODE}
        libssl${_OPENSSL_MSVC_RT_MODE}
        libssl
        ssleay32${_OPENSSL_MSVC_RT_MODE}
        ssleay32
        ssl
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        ${_OPENSSL_PATH_SUFFIXES}
    )

    set(LIB_EAY_LIBRARY_DEBUG "${LIB_EAY_DEBUG}")
    set(LIB_EAY_LIBRARY_RELEASE "${LIB_EAY_RELEASE}")
    set(SSL_EAY_LIBRARY_DEBUG "${SSL_EAY_DEBUG}")
    set(SSL_EAY_LIBRARY_RELEASE "${SSL_EAY_RELEASE}")

    include(${CMAKE_CURRENT_LIST_DIR}/SelectLibraryConfigurations.cmake)
    select_library_configurations(LIB_EAY)
    select_library_configurations(SSL_EAY)

    mark_as_advanced(LIB_EAY_LIBRARY_DEBUG LIB_EAY_LIBRARY_RELEASE
                     SSL_EAY_LIBRARY_DEBUG SSL_EAY_LIBRARY_RELEASE)
    set(OPENSSL_SSL_LIBRARY ${SSL_EAY_LIBRARY} )
    set(OPENSSL_CRYPTO_LIBRARY ${LIB_EAY_LIBRARY} )
  elseif(MINGW)
    # same player, for MinGW
    set(LIB_EAY_NAMES crypto libeay32)
    set(SSL_EAY_NAMES ssl ssleay32)
    find_library(LIB_EAY
      NAMES
        ${LIB_EAY_NAMES}
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        "lib/MinGW"
        "lib"
    )

    find_library(SSL_EAY
      NAMES
        ${SSL_EAY_NAMES}
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      PATH_SUFFIXES
        "lib/MinGW"
        "lib"
    )

    mark_as_advanced(SSL_EAY LIB_EAY)
    set(OPENSSL_SSL_LIBRARY ${SSL_EAY} )
    set(OPENSSL_CRYPTO_LIBRARY ${LIB_EAY} )
    unset(LIB_EAY_NAMES)
    unset(SSL_EAY_NAMES)
  else()
    # Not sure what to pick for -say- intel, let's use the toplevel ones and hope someone report issues:
    find_library(LIB_EAY
      NAMES
        libcrypto
        libeay32
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      HINTS
        ${_OPENSSL_LIBDIR}
      PATH_SUFFIXES
        lib
    )

    find_library(SSL_EAY
      NAMES
        libssl
        ssleay32
      NAMES_PER_DIR
      ${_OPENSSL_ROOT_HINTS_AND_PATHS}
      HINTS
        ${_OPENSSL_LIBDIR}
      PATH_SUFFIXES
        lib
    )

    mark_as_advanced(SSL_EAY LIB_EAY)
    set(OPENSSL_SSL_LIBRARY ${SSL_EAY} )
    set(OPENSSL_CRYPTO_LIBRARY ${LIB_EAY} )
  endif()
else()

  find_library(OPENSSL_SSL_LIBRARY
    NAMES
      ssl
      ssleay32
      ssleay32MD
    NAMES_PER_DIR
    ${_OPENSSL_ROOT_HINTS_AND_PATHS}
    HINTS
      ${_OPENSSL_LIBDIR}
      ${_OPENSSL_LIBRARY_DIRS}
    PATH_SUFFIXES
      lib
  )

  find_library(OPENSSL_CRYPTO_LIBRARY
    NAMES
      crypto
    NAMES_PER_DIR
    ${_OPENSSL_ROOT_HINTS_AND_PATHS}
    HINTS
      ${_OPENSSL_LIBDIR}
      ${_OPENSSL_LIBRARY_DIRS}
    PATH_SUFFIXES
      lib
  )

  mark_as_advanced(OPENSSL_CRYPTO_LIBRARY OPENSSL_SSL_LIBRARY)

endif()

set(OPENSSL_SSL_LIBRARIES ${OPENSSL_SSL_LIBRARY})
set(OPENSSL_CRYPTO_LIBRARIES ${OPENSSL_CRYPTO_LIBRARY})
set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES} )
_OpenSSL_test_and_find_dependencies("${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}")
if(_OpenSSL_has_dependencies)
  _OpenSSL_add_dependencies( OPENSSL_SSL_LIBRARIES )
  _OpenSSL_add_dependencies( OPENSSL_CRYPTO_LIBRARIES )
  _OpenSSL_add_dependencies( OPENSSL_LIBRARIES )
endif()

function(from_hex HEX DEC)
  string(TOUPPER "${HEX}" HEX)
  set(_res 0)
  string(LENGTH "${HEX}" _strlen)

  while (_strlen GREATER 0)
    math(EXPR _res "${_res} * 16")
    string(SUBSTRING "${HEX}" 0 1 NIBBLE)
    string(SUBSTRING "${HEX}" 1 -1 HEX)
    if (NIBBLE STREQUAL "A")
      math(EXPR _res "${_res} + 10")
    elseif (NIBBLE STREQUAL "B")
      math(EXPR _res "${_res} + 11")
    elseif (NIBBLE STREQUAL "C")
      math(EXPR _res "${_res} + 12")
    elseif (NIBBLE STREQUAL "D")
      math(EXPR _res "${_res} + 13")
    elseif (NIBBLE STREQUAL "E")
      math(EXPR _res "${_res} + 14")
    elseif (NIBBLE STREQUAL "F")
      math(EXPR _res "${_res} + 15")
    else()
      math(EXPR _res "${_res} + ${NIBBLE}")
    endif()

    string(LENGTH "${HEX}" _strlen)
  endwhile()

  set(${DEC} ${_res} PARENT_SCOPE)
endfunction()

if(OPENSSL_INCLUDE_DIR AND EXISTS "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h")
  file(STRINGS "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h" openssl_version_str
       REGEX "^#[\t ]*define[\t ]+OPENSSL_VERSION_NUMBER[\t ]+0x([0-9a-fA-F])+.*")

  if(openssl_version_str)
    # The version number is encoded as 0xMNNFFPPS: major minor fix patch status
    # The status gives if this is a developer or prerelease and is ignored here.
    # Major, minor, and fix directly translate into the version numbers shown in
    # the string. The patch field translates to the single character suffix that
    # indicates the bug fix state, which 00 -> nothing, 01 -> a, 02 -> b and so
    # on.

    string(REGEX REPLACE "^.*OPENSSL_VERSION_NUMBER[\t ]+0x([0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F]).*$"
           "\\1;\\2;\\3;\\4;\\5" OPENSSL_VERSION_LIST "${openssl_version_str}")
    list(GET OPENSSL_VERSION_LIST 0 OPENSSL_VERSION_MAJOR)
    list(GET OPENSSL_VERSION_LIST 1 OPENSSL_VERSION_MINOR)
    from_hex("${OPENSSL_VERSION_MINOR}" OPENSSL_VERSION_MINOR)
    list(GET OPENSSL_VERSION_LIST 2 OPENSSL_VERSION_FIX)
    from_hex("${OPENSSL_VERSION_FIX}" OPENSSL_VERSION_FIX)
    list(GET OPENSSL_VERSION_LIST 3 OPENSSL_VERSION_PATCH)

    if (NOT OPENSSL_VERSION_PATCH STREQUAL "00")
      from_hex("${OPENSSL_VERSION_PATCH}" _tmp)
      # 96 is the ASCII code of 'a' minus 1
      math(EXPR OPENSSL_VERSION_PATCH_ASCII "${_tmp} + 96")
      unset(_tmp)
      # Once anyone knows how OpenSSL would call the patch versions beyond 'z'
      # this should be updated to handle that, too. This has not happened yet
      # so it is simply ignored here for now.
      string(ASCII "${OPENSSL_VERSION_PATCH_ASCII}" OPENSSL_VERSION_PATCH_STRING)
    endif ()

    set(OPENSSL_VERSION "${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_MINOR}.${OPENSSL_VERSION_FIX}${OPENSSL_VERSION_PATCH_STRING}")
  else ()
    # Since OpenSSL 3.0.0, the new version format is MAJOR.MINOR.PATCH and
    # a new OPENSSL_VERSION_STR macro contains exactly that
    file(STRINGS "${OPENSSL_INCLUDE_DIR}/openssl/opensslv.h" OPENSSL_VERSION_STR
         REGEX "^#[\t ]*define[\t ]+OPENSSL_VERSION_STR[\t ]+\"([0-9])+\\.([0-9])+\\.([0-9])+\".*")
    string(REGEX REPLACE "^.*OPENSSL_VERSION_STR[\t ]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*$"
           "\\1" OPENSSL_VERSION_STR "${OPENSSL_VERSION_STR}")

    set(OPENSSL_VERSION "${OPENSSL_VERSION_STR}")

    unset(OPENSSL_VERSION_STR)
  endif ()
endif ()

foreach(_comp IN LISTS OpenSSL_FIND_COMPONENTS)
  if(_comp STREQUAL "Crypto")
    if(EXISTS "${OPENSSL_INCLUDE_DIR}" AND
        (EXISTS "${OPENSSL_CRYPTO_LIBRARY}" OR
        EXISTS "${LIB_EAY_LIBRARY_DEBUG}" OR
        EXISTS "${LIB_EAY_LIBRARY_RELEASE}")
    )
      set(OpenSSL_${_comp}_FOUND TRUE)
    else()
      set(OpenSSL_${_comp}_FOUND FALSE)
    endif()
  elseif(_comp STREQUAL "SSL")
    if(EXISTS "${OPENSSL_INCLUDE_DIR}" AND
        (EXISTS "${OPENSSL_SSL_LIBRARY}" OR
        EXISTS "${SSL_EAY_LIBRARY_DEBUG}" OR
        EXISTS "${SSL_EAY_LIBRARY_RELEASE}")
    )
      set(OpenSSL_${_comp}_FOUND TRUE)
    else()
      set(OpenSSL_${_comp}_FOUND FALSE)
    endif()
  else()
    message(WARNING "${_comp} is not a valid OpenSSL component")
    set(OpenSSL_${_comp}_FOUND FALSE)
  endif()
endforeach()
unset(_comp)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(OpenSSL
  REQUIRED_VARS
    OPENSSL_CRYPTO_LIBRARY
    OPENSSL_INCLUDE_DIR
  VERSION_VAR
    OPENSSL_VERSION
  HANDLE_COMPONENTS
  FAIL_MESSAGE
    "Could NOT find OpenSSL, try to set the path to OpenSSL root folder in the system variable OPENSSL_ROOT_DIR"
)

mark_as_advanced(OPENSSL_INCLUDE_DIR)

if(OPENSSL_FOUND)
  if(NOT TARGET OpenSSL::Crypto AND
      (EXISTS "${OPENSSL_CRYPTO_LIBRARY}" OR
        EXISTS "${LIB_EAY_LIBRARY_DEBUG}" OR
        EXISTS "${LIB_EAY_LIBRARY_RELEASE}")
      )
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::Crypto PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
    if(EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
      set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}")
    endif()
    if(EXISTS "${LIB_EAY_LIBRARY_RELEASE}")
      set_property(TARGET OpenSSL::Crypto APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
        IMPORTED_LOCATION_RELEASE "${LIB_EAY_LIBRARY_RELEASE}")
    endif()
    if(EXISTS "${LIB_EAY_LIBRARY_DEBUG}")
      set_property(TARGET OpenSSL::Crypto APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
        IMPORTED_LOCATION_DEBUG "${LIB_EAY_LIBRARY_DEBUG}")
    endif()
    _OpenSSL_target_add_dependencies(OpenSSL::Crypto)
  endif()

  if(NOT TARGET OpenSSL::SSL AND
      (EXISTS "${OPENSSL_SSL_LIBRARY}" OR
        EXISTS "${SSL_EAY_LIBRARY_DEBUG}" OR
        EXISTS "${SSL_EAY_LIBRARY_RELEASE}")
      )
    add_library(OpenSSL::SSL UNKNOWN IMPORTED)
    set_target_properties(OpenSSL::SSL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
    if(EXISTS "${OPENSSL_SSL_LIBRARY}")
      set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}")
    endif()
    if(EXISTS "${SSL_EAY_LIBRARY_RELEASE}")
      set_property(TARGET OpenSSL::SSL APPEND PROPERTY
        IMPORTED_CONFIGURATIONS RELEASE)
      set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
        IMPORTED_LOCATION_RELEASE "${SSL_EAY_LIBRARY_RELEASE}")
    endif()
    if(EXISTS "${SSL_EAY_LIBRARY_DEBUG}")
      set_property(TARGET OpenSSL::SSL APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG)
      set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
        IMPORTED_LOCATION_DEBUG "${SSL_EAY_LIBRARY_DEBUG}")
    endif()
    if(TARGET OpenSSL::Crypto)
      set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
    endif()
    _OpenSSL_target_add_dependencies(OpenSSL::SSL)
  endif()

  if("${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_FIX}" VERSION_GREATER_EQUAL "0.9.8")
    if(MSVC)
      if(EXISTS "${OPENSSL_INCLUDE_DIR}")
        set(_OPENSSL_applink_paths PATHS ${OPENSSL_INCLUDE_DIR})
      endif()
      find_file(OPENSSL_APPLINK_SOURCE
        NAMES
          openssl/applink.c
        ${_OPENSSL_applink_paths}
        NO_DEFAULT_PATH)
      if(OPENSSL_APPLINK_SOURCE)
        set(_OPENSSL_applink_interface_srcs ${OPENSSL_APPLINK_SOURCE})
      endif()
    endif()
    if(NOT TARGET OpenSSL::applink)
      add_library(OpenSSL::applink INTERFACE IMPORTED)
      set_property(TARGET OpenSSL::applink APPEND
        PROPERTY INTERFACE_SOURCES
          ${_OPENSSL_applink_interface_srcs})
    endif()
  endif()
endif()

# Restore the original find library ordering
if(OPENSSL_USE_STATIC_LIBS)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${_openssl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
endif()
