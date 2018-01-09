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

if("${gRPC_SSL_PROVIDER}" STREQUAL "module")
  # Build dependency as external project from git submodule

  include(ExternalProject)

  if(CMAKE_ASM_NASM_COMPILER)
    # TODO(jtattermusch): needed to correctly locate yasm.exe on jenkins, remove once not needed
    set(_gRPC_BORINGSSL_ASM_ARGS -DCMAKE_ASM_NASM_COMPILER:PATH="${CMAKE_ASM_NASM_COMPILER}")
  endif()

  ExternalProject_Add(boringssl
    PREFIX boringssl
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boringssl"
    INSTALL_COMMAND ""
    CMAKE_CACHE_ARGS
          ${_gRPC_EP_COMMON_ARGS}
          # make boringssl buildable with Visual Studio
          -DOPENSSL_NO_ASM:BOOL=ON
          ${_gRPC_BORINGSSL_ASM_ARGS}
  )

  add_library(ssl::ssl STATIC IMPORTED)
  add_dependencies(ssl::ssl boringssl)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET ssl::ssl PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/ssl/Release/ssl.lib)
    set_property(TARGET ssl::ssl PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/ssl/Debug/ssl.lib)
  else()
    set_property(TARGET ssl::ssl PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/ssl/libssl.a)
  endif()

  add_library(ssl::crypto STATIC IMPORTED)
  add_dependencies(ssl::crypto boringssl)  # add dependency on the external project
  if(WIN32)
    set_property(TARGET ssl::crypto PROPERTY IMPORTED_LOCATION_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/crypto/Release/crypto.lib)
    set_property(TARGET ssl::crypto PROPERTY IMPORTED_LOCATION_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/crypto/Debug/crypto.lib)
  else()
    set_property(TARGET ssl::crypto PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/boringssl/src/boringssl-build/crypto/libcrypto.a)
  endif()

  set(_gRPC_SSL_LIBRARIES ssl::ssl ssl::crypto)
  set(_gRPC_SSL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boringssl/include)

elseif("${gRPC_SSL_PROVIDER}" STREQUAL "package")
  # Find pre-installed dependency

  find_package(OpenSSL REQUIRED)
  
  if(TARGET OpenSSL::SSL)
    set(_gRPC_SSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  else()
    set(_gRPC_SSL_LIBRARIES ${OPENSSL_LIBRARIES})
  endif()
  set(_gRPC_SSL_INCLUDE_DIR ${OPENSSL_INCLUDE_DIR})
  
  set(_gRPC_FIND_SSL "if(NOT OPENSSL_FOUND)\n  find_package(OpenSSL)\nendif()")

endif()
