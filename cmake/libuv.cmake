# Copyright 2021 gRPC authors.
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

if(NOT LIBUV_ROOT_DIR)
  set(LIBUV_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libuv)
endif()
set(_gRPC_LIBUV_INCLUDE_DIR "${LIBUV_ROOT_DIR}/include" "${LIBUV_ROOT_DIR}/src")

if(gRPC_LIBUV_PROVIDER STREQUAL "module")
  include_directories("${LIBUV_ROOT_DIR}/include" "${LIBUV_ROOT_DIR}/src")
  add_subdirectory("${LIBUV_ROOT_DIR}" third_party/libuv)
  if(TARGET uv)
    set(_gRPC_LIBUV_LIBRARIES uv)
    if(gRPC_INSTALL)
      if(_gRPC_INSTALL_SUPPORTED_FROM_MODULE)
        install(TARGETS uv EXPORT gRPCTargets
          RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
          LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
      else()
        message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_LIBUV_PROVIDER is \"module\" and CMake version (${CMAKE_VERSION}) is less than 3.13.")
        set(gRPC_INSTALL FALSE)
      endif()
    endif()
  endif()
elseif(gRPC_LIBUV_PROVIDER STREQUAL "package")
  find_package(libuv REQUIRED)
  if(TARGET libuv::uv)
    set(_gRPC_LIBUV_LIBRARIES libuv::uv)
  endif()
  set(_gRPC_FIND_LIBUV "if(NOT libuv_FOUND)\n  find_package(libuv)\nendif()")
endif()
