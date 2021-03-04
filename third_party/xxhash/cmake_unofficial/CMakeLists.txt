# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to
# the public domain worldwide. This software is distributed without
# any warranty.
#
# For details, see <https://creativecommons.org/publicdomain/zero/1.0/>.

cmake_minimum_required (VERSION 2.8.12 FATAL_ERROR)

set(XXHASH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_MAJOR REGEX "^#define XXH_VERSION_MAJOR +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_MAJOR +([0-9]+) *$" "\\1" XXHASH_VERSION_MAJOR "${XXHASH_VERSION_MAJOR}")
file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_MINOR REGEX "^#define XXH_VERSION_MINOR +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_MINOR +([0-9]+) *$" "\\1" XXHASH_VERSION_MINOR "${XXHASH_VERSION_MINOR}")
file(STRINGS "${XXHASH_DIR}/xxhash.h" XXHASH_VERSION_RELEASE REGEX "^#define XXH_VERSION_RELEASE +([0-9]+) *$")
string(REGEX REPLACE "^#define XXH_VERSION_RELEASE +([0-9]+) *$" "\\1" XXHASH_VERSION_RELEASE "${XXHASH_VERSION_RELEASE}")
set(XXHASH_VERSION_STRING "${XXHASH_VERSION_MAJOR}.${XXHASH_VERSION_MINOR}.${XXHASH_VERSION_RELEASE}")
set(XXHASH_LIB_VERSION ${XXHASH_VERSION_STRING})
set(XXHASH_LIB_SOVERSION "${XXHASH_VERSION_MAJOR}")
mark_as_advanced(XXHASH_VERSION_MAJOR XXHASH_VERSION_MINOR XXHASH_VERSION_RELEASE XXHASH_VERSION_STRING XXHASH_LIB_VERSION XXHASH_LIB_SOVERSION)

if("${CMAKE_VERSION}" VERSION_LESS "3.13")
    #message(WARNING "CMake ${CMAKE_VERSION} has no CMP0077 policy: options will erase uncached/untyped normal vars!")
else()
    cmake_policy (SET CMP0077 NEW)
endif()
if("${CMAKE_VERSION}" VERSION_LESS "3.0")
    project(xxHash C)
else()
    cmake_policy (SET CMP0048 NEW)
    project(xxHash
        VERSION ${XXHASH_VERSION_STRING}
        LANGUAGES C)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Project build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "xxHash build type: ${CMAKE_BUILD_TYPE}")
endif()

option(BUILD_SHARED_LIBS "Build shared library" ON)
option(XXHASH_BUILD_XXHSUM "Build the xxhsum binary" ON)

# If XXHASH is being bundled in another project, we don't want to
# install anything.  However, we want to let people override this, so
# we'll use the XXHASH_BUNDLED_MODE variable to let them do that; just
# set it to OFF in your project before you add_subdirectory(xxhash/cmake_unofficial).
if(NOT DEFINED XXHASH_BUNDLED_MODE)
  if("${PROJECT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
    set(XXHASH_BUNDLED_MODE OFF)
  else()
    set(XXHASH_BUNDLED_MODE ON)
  endif()
endif()
set(XXHASH_BUNDLED_MODE ${XXHASH_BUNDLED_MODE} CACHE BOOL "" FORCE)
mark_as_advanced(XXHASH_BUNDLED_MODE)

# Allow people to choose whether to build shared or static libraries
# via the BUILD_SHARED_LIBS option unless we are in bundled mode, in
# which case we always use static libraries.
include(CMakeDependentOption)
CMAKE_DEPENDENT_OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON "NOT XXHASH_BUNDLED_MODE" OFF)

# libxxhash
add_library(xxhash "${XXHASH_DIR}/xxhash.c")
add_library(${PROJECT_NAME}::xxhash ALIAS xxhash)

target_include_directories(xxhash
  PUBLIC
    $<BUILD_INTERFACE:${XXHASH_DIR}>
    $<INSTALL_INTERFACE:include/>)
if (BUILD_SHARED_LIBS)
  target_compile_definitions(xxhash PUBLIC XXH_EXPORT)
endif ()
set_target_properties(xxhash PROPERTIES
  SOVERSION "${XXHASH_VERSION_STRING}"
  VERSION "${XXHASH_VERSION_STRING}")

if(XXHASH_BUILD_XXHSUM)
  set(XXHSUM_DIR "${XXHASH_DIR}/cli")
  # xxhsum
  add_executable(xxhsum "${XXHASH_DIR}/xxhsum.c"
                        "${XXHSUM_DIR}/xsum_os_specific.c"
                        "${XXHSUM_DIR}/xsum_output.c"
                        "${XXHSUM_DIR}/xsum_sanity_check.c"
                )
  add_executable(${PROJECT_NAME}::xxhsum ALIAS xxhsum)

  target_link_libraries(xxhsum PRIVATE xxhash)
  target_include_directories(xxhsum PRIVATE "${XXHASH_DIR}")
endif(XXHASH_BUILD_XXHSUM)

# Extra warning flags
include (CheckCCompilerFlag)
if (XXHASH_C_FLAGS)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${XXHASH_C_FLAGS}")
endif()
foreach (flag
    -Wall -Wextra -Wcast-qual -Wcast-align -Wshadow
    -Wstrict-aliasing=1 -Wswitch-enum -Wdeclaration-after-statement
    -Wstrict-prototypes -Wundef)
  # Because https://gcc.gnu.org/wiki/FAQ#wnowarning
  string(REGEX REPLACE "\\-Wno\\-(.+)" "-W\\1" flag_to_test "${flag}")
  string(REGEX REPLACE "[^a-zA-Z0-9]+" "_" test_name "CFLAG_${flag_to_test}")

  check_c_compiler_flag("${ADD_COMPILER_FLAGS_PREPEND} ${flag_to_test}" ${test_name})

  if(${test_name})
    set(CMAKE_C_FLAGS "${flag} ${CMAKE_C_FLAGS}")
  endif()

  unset(test_name)
  unset(flag_to_test)
endforeach (flag)

if(NOT XXHASH_BUNDLED_MODE)
  include(GNUInstallDirs)

  install(TARGETS xxhash
    EXPORT xxHashTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(FILES "${XXHASH_DIR}/xxhash.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES "${XXHASH_DIR}/xxh3.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  if(XXHASH_BUILD_XXHSUM)
    install(TARGETS xxhsum
      EXPORT xxHashTargets
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${XXHASH_DIR}/xxhsum.1"
      DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
  endif(XXHASH_BUILD_XXHSUM)

  include(CMakePackageConfigHelpers)

  set(xxHash_VERSION_CONFIG "${PROJECT_BINARY_DIR}/xxHashConfigVersion.cmake")
  set(xxHash_PROJECT_CONFIG "${PROJECT_BINARY_DIR}/xxHashConfig.cmake")
  set(xxHash_TARGETS_CONFIG "${PROJECT_BINARY_DIR}/xxHashTargets.cmake")
  set(xxHash_CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/xxHash/")
  write_basic_package_version_file(${xxHash_VERSION_CONFIG}
    VERSION ${XXHASH_VERSION_STRING}
    COMPATIBILITY AnyNewerVersion)
  configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/xxHashConfig.cmake.in
    ${xxHash_PROJECT_CONFIG}
    INSTALL_DESTINATION ${xxHash_CONFIG_INSTALL_DIR})
  if("${CMAKE_VERSION}" VERSION_LESS "3.0")
      set(XXHASH_EXPORT_SET xxhash)
      if(XXHASH_BUILD_XXHSUM)
        set(XXHASH_EXPORT_SET ${XXHASH_EXPORT_SET} xxhsum)
      endif()
      export(TARGETS ${XXHASH_EXPORT_SET}
      FILE ${xxHash_TARGETS_CONFIG}
      NAMESPACE ${PROJECT_NAME}::)
  else()
    export(EXPORT xxHashTargets
      FILE ${xxHash_TARGETS_CONFIG}
      NAMESPACE ${PROJECT_NAME}::)
  endif()

  install(FILES ${xxHash_PROJECT_CONFIG} ${xxHash_VERSION_CONFIG}
    DESTINATION ${xxHash_CONFIG_INSTALL_DIR})
  install(EXPORT xxHashTargets
    DESTINATION ${xxHash_CONFIG_INSTALL_DIR}
    NAMESPACE ${PROJECT_NAME}::)
endif(NOT XXHASH_BUNDLED_MODE)
