# Copyright 2020 gRPC authors.
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

if(gRPC_LIBUV_PROVIDER STREQUAL "module")
  if(NOT LIBUV_ROOT_DIR)
    set(LIBUV_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libuv)
  endif()
  if(EXISTS "${LIBUV_ROOT_DIR}/CMakeLists.txt")
    add_subdirectory(${LIBUV_ROOT_DIR} third_party/libuv)
    if(TARGET uv_a)
      set(_gRPC_LIBUV_LIBRARIES uv_a)
      set(_gRPC_LIBUV_INCLUDE_DIR "${LIBUV_ROOT_DIR}/include")
      if(gRPC_INSTALL AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
        install(TARGETS uv_a EXPORT gRPCTargets
          RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
          LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
      endif()
    endif()
  else()
	  message(WARNING "gRPC_LIBUV_PROVIDER is \"module\" but LIBUV_ROOT_DIR is wrong")
  endif()
  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_LIBUV_PROVIDER is \"module\" and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_LIBUV_PROVIDER STREQUAL "package")
  # libuv installation directory can be configured by setting LibUV_ROOT.
  find_package(LibUV 1.35.0 REQUIRED)
  if(TARGET LibUV::LibUV)
    set(_gRPC_LIBUV_LIBRARIES LibUV::LibUV)
  endif()
  set(_gRPC_FIND_LIBUV "if(NOT LibUV_FOUND)\n  find_package(LibUV)\nendif()")
endif()
