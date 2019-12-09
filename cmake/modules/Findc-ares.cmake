include(FindPackageHandleStandardArgs)

function(__cares_get_version)
  if(c-ares_INCLUDE_DIR AND EXISTS "${c-ares_INCLUDE_DIR}/ares_version.h")
    file(STRINGS "${c-ares_INCLUDE_DIR}/ares_version.h" _cares_version_str REGEX "^#define ARES_VERSION_STR \"([^\n]*)\"$")
    if(_cares_version_str MATCHES "#define ARES_VERSION_STR \"([^\n]*)\"")
      set(c-ares_VERSION "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# We need to disable version checking, since c-ares does not provide it.
set(_cares_version_var_suffixes "" _MAJOR _MINOR _PATCH _TWEAK _COUNT)
foreach(_suffix IN LISTS _cares_version_var_suffixes)
  set(_cares_save_FIND_VERSION${_suffix} ${c-ares_FIND_VERSION${_suffix}})
  unset(c-ares_FIND_VERSION${_suffix})
endforeach()
find_package(c-ares CONFIG)
foreach(_suffix IN LISTS _cares_version_var_suffixes)
  set(c-ares_FIND_VERSION${_suffix} ${_cares_save_FIND_VERSION${_suffix}})
endforeach()

if(c-ares_FOUND)
  if(NOT DEFINED c-ares_VERSION)
    __cares_get_version()
  endif()

  find_package_handle_standard_args(c-ares CONFIG_MODE)
  return()
endif()

find_path(c-ares_INCLUDE_DIR NAMES ares.h)
__cares_get_version()

find_library(c-ares_LIBRARY cares)

find_package_handle_standard_args(c-ares
  REQUIRED_VARS c-ares_INCLUDE_DIR c-ares_LIBRARY
  VERSION_VAR c-ares_VERSION
  )

if(c-ares_FOUND)
  add_library(c-ares::cares UNKNOWN IMPORTED)
  set_target_properties(c-ares::cares PROPERTIES
    IMPORTED_LOCATION "${c-ares_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${c-ares_INCLUDE_DIR}"
    )
endif()
