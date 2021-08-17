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

if (ABSL_ROOT_DIR)
  set(S2A_CORE_ABSL_DIR ${ABSL_ROOT_DIR})
else()
  set(S2A_CORE_ABSL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp)
endif()

if (_gRPC_SSL_INCLUDE_DIR)
  set(S2A_CORE_SSL_INCLUDE_DIR ${_gRPC_SSL_INCLUDE_DIR})
  set(S2A_CORE_SSL_LIBRARIES ${_gRPC_SSL_LIBRARIES})
endif()

if (_gRPC_UPB_INCLUDE_DIR)
  set(S2A_CORE_UPB_INCLUDE_DIR ${_gRPC_UPB_INCLUDE_DIR})
endif()

if (_gRPC_UPB_LIBRARIES)
  set(S2A_CORE_UPB_LIBRARY ${_gRPC_UPB_LIBRARIES})
endif()

if(gRPC_S2A_CORE_PROVIDER STREQUAL "module")
  if(NOT S2A_CORE_ROOT_DIR)
    set(S2A_CORE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/s2a_core")
  endif()
  if(EXISTS "${S2A_CORE_ROOT_DIR}/CMakeLists.txt")
    include_directories("${S2A_CORE_ROOT_DIR}")
    add_subdirectory(${S2A_CORE_ROOT_DIR} third_party/s2a_core)

    if(TARGET s2a_core)
      set(_gRPC_S2A_CORE_LIBRARIES s2a_core)
      set(_gRPC_S2A_CORE_INCLUDE_DIR "${S2A_CORE_ROOT_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/s2a_core")
      if(gRPC_INSTALL AND _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
        install(TARGETS s2a_core EXPORT gRPCTargets
          RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
          LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
          ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR})
      endif()
    endif()
  else()
      message(WARNING "gRPC_S2A_CORE_PROVIDER is \"module\" but S2A_CORE_ROOT_DIR(${S2A_CORE_ROOT_DIR}) is wrong")
  endif()
  if(gRPC_INSTALL AND NOT _gRPC_INSTALL_SUPPORTED_FROM_MODULE)
      message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_S2A_CORE_PROVIDER is \"module\"  and CMake version (${CMAKE_VERSION}) is less than 3.13.")
    set(gRPC_INSTALL FALSE)
  endif()
elseif(gRPC_S2A_CORE_PROVIDER STREQUAL "package")
  find_package(s2a_core REQUIRED CONFIG)
  set(_gRPC_FIND_S2A_CORE "if(NOT s2a_core_FOUND)\n  find_package(s2a_core)\nendif()")
endif()
