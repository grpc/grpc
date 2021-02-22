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

# The CMakeLists.txt for xxhash doesn't propagate include directories
# transitively so `_gRPC_XXHASH_INCLUDE_DIR` should be set for gRPC
# to find header files.

if(gRPC_XXHASH_PROVIDER STREQUAL "module")
	if(NOT XXHASH_ROOT_DIR)
		set(XXHASH_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash/cmake_unofficial)
  endif()
  if(EXISTS "${XXHASH_ROOT_DIR}/CMakeLists.txt")
    # Explicitly disable BUILD_TESTING to avoid xxhash's CMakeLists.txt triggering https://github.com/grpc/grpc/issues/23586
    option(BUILD_TESTING "xxhash.cmake explicitly disabled CTest's BUILD_TESTING option." OFF)

    include_directories("${XXHASH_ROOT_DIR}")
    add_subdirectory(${XXHASH_ROOT_DIR} third_party/xxhash)

    if(TARGET xxhash)
	    set(_gRPC_XXHASH_LIBRARIES xxhash)
	    set(_gRPC_XXHASH_INCLUDE_DIR "${XXHASH_ROOT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/xxhash")
      if(gRPC_INSTALL AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
        install(TARGETS xxhash EXPORT gRPCTargets
          RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
          LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
      endif()
    endif()
  else()
	  message(WARNING "gRPC_XXHASH_PROVIDER is \"module\" but XXHASH_ROOT_DIR(${XXHASH_ROOT_DIR}) is wrong")
  endif()
  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
	  message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_XXHASH_PROVIDER is \"module\"  and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_XXHASH_PROVIDER STREQUAL "package")
	find_package(xxHash REQUIRED)
  if(TARGET xxhash)
	  set(_gRPC_XXHASH_LIBRARIES xxhash)
  endif()
  set(_gRPC_FIND_XXHASH "if(NOT xxHash_FOUND)\n  find_package(xxHash)\nendif()")
endif()
