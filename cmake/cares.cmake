# Copyright 2017 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if(gRPC_CARES_PROVIDER STREQUAL "module")
  if(NOT CARES_ROOT_DIR)
    set(CARES_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cares/cares)
  endif()
  set(CARES_SHARED OFF CACHE BOOL "disable shared library")
  set(CARES_STATIC ON CACHE BOOL "link cares statically")
  if(gRPC_BACKWARDS_COMPATIBILITY_MODE)
    # See https://github.com/grpc/grpc/issues/17255
    set(HAVE_LIBNSL OFF CACHE BOOL "avoid cares dependency on libnsl")
  endif()
  add_subdirectory("${CARES_ROOT_DIR}" third_party/cares/cares)

  if(TARGET c-ares)
    set(_gRPC_CARES_LIBRARIES c-ares)
    if(gRPC_INSTALL AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
      install(TARGETS c-ares EXPORT gRPCTargets
        RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
        LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
    endif()
  endif()

  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_CARES_PROVIDER is \"module\" and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_CARES_PROVIDER STREQUAL "package")
  find_package(c-ares 1.13.0 REQUIRED)
  if(TARGET c-ares::cares)
    set(_gRPC_CARES_LIBRARIES c-ares::cares)
  endif()
  set(_gRPC_FIND_CARES "if(NOT c-ares_FOUND)\n  find_package(c-ares)\nendif()")
endif()
