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
  if(NOT BORINGSSL_ROOT_DIR)
    set(BORINGSSL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boringssl)
  endif()
  if(EXISTS "${BORINGSSL_ROOT_DIR}/CMakeLists.txt")
    set(OPENSSL_NO_ASM ON)  # make boringssl buildable with Visual Studio
    add_subdirectory(${BORINGSSL_ROOT_DIR} third_party/boringssl)
    if(TARGET ssl)
      set(_gRPC_SSL_LIBRARIES ssl)
      set(_gRPC_SSL_INCLUDE_DIR ${BORINGSSL_ROOT_DIR}/include)
    endif()
  else()
      message(WARNING "gRPC_SSL_PROVIDER is \"module\" but BORINGSSL_ROOT_DIR is wrong")
  endif()
  if(gRPC_INSTALL)
    message(WARNING "gRPC_INSTALL will be forced to FALSE because gRPC_SSL_PROVIDER is \"module\"")
    set(gRPC_INSTALL FALSE)
  endif()
elseif("${gRPC_SSL_PROVIDER}" STREQUAL "package")
  # OpenSSL installation directory can be configured by setting OPENSSL_ROOT_DIR
  # We expect to locate OpenSSL using the built-in cmake module as the openssl
  # project itself does not provide installation support in its CMakeLists.txt
  # See https://cmake.org/cmake/help/v3.6/module/FindOpenSSL.html
  find_package(OpenSSL REQUIRED)
  
  if(TARGET OpenSSL::SSL)
    set(_gRPC_SSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
  else()
    set(_gRPC_SSL_LIBRARIES ${OPENSSL_LIBRARIES})
  endif()
  set(_gRPC_SSL_INCLUDE_DIR ${OPENSSL_INCLUDE_DIR})
  
  set(_gRPC_FIND_SSL "if(NOT OPENSSL_FOUND)\n  find_package(OpenSSL)\nendif()")
endif()
