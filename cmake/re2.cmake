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

# The CMakeLists.txt for re2 doesn't propagate include directories
# transitively so `_gRPC_RE2_INCLUDE_DIR` should be set for gRPC
# to find header files.

if(gRPC_RE2_PROVIDER STREQUAL "module")
  if(NOT RE2_ROOT_DIR)
    set(RE2_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/re2)
  endif()
  if(EXISTS "${RE2_ROOT_DIR}/CMakeLists.txt")
    # Explicitly disable BUILD_TESTING to avoid re2's CMakeLists.txt triggering https://github.com/grpc/grpc/issues/23586
    option(BUILD_TESTING "re2.cmake explicitly disabled CTest's BUILD_TESTING option." OFF)

    include_directories("${RE2_ROOT_DIR}")
    add_subdirectory(${RE2_ROOT_DIR} third_party/re2)

    if(TARGET re2)
      set(_gRPC_RE2_LIBRARIES re2)
      set(_gRPC_RE2_INCLUDE_DIR "${RE2_ROOT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/re2")
      if(gRPC_INSTALL AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
        install(TARGETS re2 EXPORT gRPCTargets
          RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
          LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
      endif()
    endif()
  else()
    message(WARNING "gRPC_RE2_PROVIDER is \"module\" but RE2_ROOT_DIR(${RE2_ROOT_DIR}) is wrong")
  endif()
  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_RE2_PROVIDER is \"module\"  and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_RE2_PROVIDER STREQUAL "package")
  find_package(re2 REQUIRED)
  if(TARGET re2::re2)
    set(_gRPC_RE2_LIBRARIES re2::re2)
  endif()
  set(_gRPC_FIND_RE2 "if(NOT re2_FOUND)\n  find_package(re2)\nendif()")
endif()
