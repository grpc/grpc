# GRPC global cmake file
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh
#
# Copyright 2015 gRPC authors.
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




cmake_minimum_required(VERSION 3.5.1)

set(PACKAGE_NAME          "grpc")
set(PACKAGE_VERSION       "1.38.0")
set(gRPC_CORE_VERSION     "16.0.0")
set(gRPC_CORE_SOVERSION   "16")
set(gRPC_CPP_VERSION      "1.38.0")
set(gRPC_CPP_SOVERSION    "1.38")
set(gRPC_CSHARP_VERSION   "2.38.0")
set(gRPC_CSHARP_SOVERSION "2.38")
set(PACKAGE_STRING        "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME       "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT     "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} LANGUAGES C CXX)

set(gRPC_INSTALL_BINDIR "bin" CACHE STRING "Installation directory for executables")
set(gRPC_INSTALL_LIBDIR "lib" CACHE STRING "Installation directory for libraries")
set(gRPC_INSTALL_INCLUDEDIR "include" CACHE STRING "Installation directory for headers")
set(gRPC_INSTALL_CMAKEDIR "lib/cmake/${PACKAGE_NAME}" CACHE STRING "Installation directory for cmake config files")
set(gRPC_INSTALL_SHAREDIR "share/grpc" CACHE STRING "Installation directory for root certificates")

# Options
option(gRPC_BUILD_TESTS "Build tests" OFF)
option(gRPC_BUILD_CODEGEN "Build codegen" ON)
option(gRPC_BUILD_CSHARP_EXT "Build C# extensions" ON)
option(gRPC_BACKWARDS_COMPATIBILITY_MODE "Build libraries that are binary compatible across a larger number of OS and libc versions" OFF)

set(gRPC_INSTALL_default ON)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Disable gRPC_INSTALL by default if building as a submodule
  set(gRPC_INSTALL_default OFF)
endif()
set(gRPC_INSTALL ${gRPC_INSTALL_default} CACHE BOOL
    "Generate installation target")

# We can install dependencies from submodules if we're running
# CMake v3.13 or newer.
if(CMAKE_VERSION VERSION_LESS 3.13)
  set(_gRPC_INSTALL_SUPPORTED_FROM_MODULE OFF)
else()
  set(_gRPC_INSTALL_SUPPORTED_FROM_MODULE ON)
endif()

# Providers for third-party dependencies (gRPC_*_PROVIDER properties):
# "module": build the dependency using sources from git submodule (under third_party)
# "package": use cmake's find_package functionality to locate a pre-installed dependency

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_CARES_PROVIDER "module" CACHE STRING "Provider of c-ares library")
set_property(CACHE gRPC_CARES_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_RE2_PROVIDER "module" CACHE STRING "Provider of re2 library")
set_property(CACHE gRPC_RE2_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PACKAGE_TYPE "" CACHE STRING "Algorithm for searching protobuf package")
set_property(CACHE gRPC_PROTOBUF_PACKAGE_TYPE PROPERTY STRINGS "CONFIG" "MODULE")

if(gRPC_BUILD_TESTS)
  set(gRPC_BENCHMARK_PROVIDER "module" CACHE STRING "Provider of benchmark library")
  set_property(CACHE gRPC_BENCHMARK_PROVIDER PROPERTY STRINGS "module" "package")
else()
  set(gRPC_BENCHMARK_PROVIDER "none")
endif()

set(gRPC_ABSL_PROVIDER "module" CACHE STRING "Provider of absl library")
set_property(CACHE gRPC_ABSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_ABSL_USED_TARGETS
  absl_algorithm
  absl_algorithm_container
  absl_atomic_hook
  absl_bad_optional_access
  absl_bad_variant_access
  absl_base
  absl_base_internal
  absl_bind_front
  absl_bits
  absl_city
  absl_civil_time
  absl_compressed_tuple
  absl_config
  absl_container_common
  absl_container_memory
  absl_cord
  absl_core_headers
  absl_debugging_internal
  absl_demangle_internal
  absl_dynamic_annotations
  absl_endian
  absl_errno_saver
  absl_exponential_biased
  absl_fixed_array
  absl_flat_hash_map
  absl_function_ref
  absl_graphcycles_internal
  absl_hash
  absl_hash_function_defaults
  absl_hash_policy_traits
  absl_hashtable_debug_hooks
  absl_hashtablez_sampler
  absl_have_sse
  absl_inlined_vector
  absl_inlined_vector_internal
  absl_int128
  absl_kernel_timeout_internal
  absl_layout
  absl_log_severity
  absl_malloc_internal
  absl_memory
  absl_numeric_representation
  absl_optional
  absl_raw_hash_map
  absl_raw_hash_set
  absl_raw_logging_internal
  absl_span
  absl_spinlock_wait
  absl_stacktrace
  absl_status
  absl_statusor
  absl_str_format
  absl_str_format_internal
  absl_strings
  absl_strings_internal
  absl_symbolize
  absl_synchronization
  absl_throw_delegate
  absl_time
  absl_time_zone
  absl_type_traits
  absl_utility
  absl_variant
  absl_wyhash
  absl_meta
)

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if(UNIX)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(_gRPC_PLATFORM_LINUX ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(_gRPC_PLATFORM_MAC ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "iOS")
    set(_gRPC_PLATFORM_IOS ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    set(_gRPC_PLATFORM_ANDROID ON)
  else()
    set(_gRPC_PLATFORM_POSIX ON)
  endif()
endif()
if(WIN32)
  set(_gRPC_PLATFORM_WINDOWS ON)
endif()

 # Use C99 standard
if (NOT DEFINED CMAKE_C_STANDARD)
  set(CMAKE_C_STANDARD 99)
endif()

# Add c++11 flags
if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
else()
  if (CMAKE_CXX_STANDARD LESS 11)
    message(FATAL_ERROR "CMAKE_CXX_STANDARD is less than 11, please specify at least SET(CMAKE_CXX_STANDARD 11)")
  endif()
endif()
if (NOT DEFINED CMAKE_CXX_STANDARD_REQUIRED)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
if (NOT DEFINED CMAKE_CXX_EXTENSIONS)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if (NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

if(MSVC)
  include(cmake/msvc_static_runtime.cmake)
  add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # needed to compile protobuf
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4065 /wd4506")
  # TODO(jtattermusch): revisit warnings that were silenced as part of upgrade to protobuf3.6.0
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4200 /wd4291 /wd4244")
  # TODO(jtattermusch): revisit C4267 occurrences throughout the code
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4267")
  # TODO(jtattermusch): needed to build boringssl with VS2017, revisit later
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4987 /wd4774 /wd4819 /wd4996 /wd4619")
endif()
if (MINGW)
  add_definitions(-D_WIN32_WINNT=0x600)
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_gRPC_C_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_gRPC_C_CXX_FLAGS}")

if(gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

if(gRPC_BACKWARDS_COMPATIBILITY_MODE)
  add_definitions(-DGPR_BACKWARDS_COMPATIBILITY_MODE)
  if(_gRPC_PLATFORM_MAC)
    # some C++11 constructs not supported before OS X 10.10
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.10)
  endif()
endif()

if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_IOS)
  set(_gRPC_CORE_NOSTDCXX_FLAGS -fno-exceptions -fno-rtti)
else()
  set(_gRPC_CORE_NOSTDCXX_FLAGS "")
endif()

include(cmake/abseil-cpp.cmake)
include(cmake/address_sorting.cmake)
include(cmake/benchmark.cmake)
include(cmake/cares.cmake)
include(cmake/protobuf.cmake)
include(cmake/re2.cmake)
include(cmake/ssl.cmake)
include(cmake/upb.cmake)
include(cmake/xxhash.cmake)
include(cmake/zlib.cmake)

if(_gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_IOS)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m pthread)
elseif(_gRPC_PLATFORM_ANDROID)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m)
elseif(UNIX)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} rt m pthread)
endif()

if(WIN32)
  set(_gRPC_BASELIB_LIBRARIES wsock32 ws2_32 crypt32)
endif()

# Create directory for generated .proto files
set(_gRPC_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
file(MAKE_DIRECTORY ${_gRPC_PROTO_GENS_DIR})

#  protobuf_generate_grpc_cpp
#  --------------------------
#
#   Add custom commands to process ``.proto`` files to C++ using protoc and
#   GRPC plugin::
#
#     protobuf_generate_grpc_cpp [<ARGN>...]
#
#   ``ARGN``
#     ``.proto`` files
#
function(protobuf_generate_grpc_cpp)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_GRPC_CPP() called without any proto files")
    return()
  endif()

  set(_protobuf_include_path -I . -I ${_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR})
  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    file(RELATIVE_PATH REL_FIL ${CMAKE_CURRENT_SOURCE_DIR} ${ABS_FIL})
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")

    #if cross-compiling, find host plugin
    if(CMAKE_CROSSCOMPILING)
      find_program(_gRPC_CPP_PLUGIN grpc_cpp_plugin)
    else()
      set(_gRPC_CPP_PLUGIN $<TARGET_FILE:grpc_cpp_plugin>)
    endif()

    add_custom_command(
      OUTPUT "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
      COMMAND ${_gRPC_PROTOBUF_PROTOC_EXECUTABLE}
      ARGS --grpc_out=generate_mock_code=true:${_gRPC_PROTO_GENS_DIR}
           --cpp_out=${_gRPC_PROTO_GENS_DIR}
           --plugin=protoc-gen-grpc=${_gRPC_CPP_PLUGIN}
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_gRPC_PROTOBUF_PROTOC} grpc_cpp_plugin
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
      VERBATIM)
  endforeach()
endfunction()

# These options allow users to enable or disable the building of the various
# protoc plugins. For example, running CMake with
# -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF will disable building the C# plugin.
set(_gRPC_PLUGIN_LIST)
option(gRPC_BUILD_GRPC_CPP_PLUGIN "Build grpc_cpp_plugin" ON)
if (gRPC_BUILD_GRPC_CPP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_cpp_plugin)
endif ()
option(gRPC_BUILD_GRPC_CSHARP_PLUGIN "Build grpc_csharp_plugin" ON)
if (gRPC_BUILD_GRPC_CSHARP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_csharp_plugin)
endif ()
option(gRPC_BUILD_GRPC_NODE_PLUGIN "Build grpc_node_plugin" ON)
if (gRPC_BUILD_GRPC_NODE_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_node_plugin)
endif ()
option(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN "Build grpc_objective_c_plugin" ON)
if (gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_objective_c_plugin)
endif ()
option(gRPC_BUILD_GRPC_PHP_PLUGIN "Build grpc_php_plugin" ON)
if (gRPC_BUILD_GRPC_PHP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_php_plugin)
endif ()
option(gRPC_BUILD_GRPC_PYTHON_PLUGIN "Build grpc_python_plugin" ON)
if (gRPC_BUILD_GRPC_PYTHON_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_python_plugin)
endif ()
option(gRPC_BUILD_GRPC_RUBY_PLUGIN "Build grpc_ruby_plugin" ON)
if (gRPC_BUILD_GRPC_RUBY_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_ruby_plugin)
endif ()

add_custom_target(plugins
  DEPENDS ${_gRPC_PLUGIN_LIST}
)

add_custom_target(tools_c
  DEPENDS
  check_epollexclusive
)

add_custom_target(tools_cxx
  DEPENDS
  gen_hpack_tables
  gen_legal_metadata_characters
  gen_percent_encoding_tables
)

add_custom_target(tools
  DEPENDS tools_c tools_cxx)

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/core/stats.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/health/v1/health.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/lb/v1/load_balancer.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/status/status.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/benchmark_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/control.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/duplicate/echo_duplicate.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/payloads.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/report_qps_scenario_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/simple_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/stats.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/worker_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/ads_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/cds_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/eds_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/lds_rds_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/lrs_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/orca_load_report_for_test.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/address.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/ads.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/aggregate_cluster.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/base.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/cluster.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/config_dump.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/config_source.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/csds.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/discovery.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/endpoint.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/fault.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/fault_common.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/http_connection_manager.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/listener.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/load_report.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/lrs.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/percent.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/protocol.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/range.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/regex.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/route.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/router.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/string.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/xds/v3/tls.proto
)
protobuf_generate_grpc_cpp(
  test/core/tsi/alts/fake_handshaker/handshaker.proto
)
protobuf_generate_grpc_cpp(
  test/core/tsi/alts/fake_handshaker/transport_security_common.proto
)

if(gRPC_BUILD_TESTS)
  add_custom_target(buildtests_c)
  add_dependencies(buildtests_c algorithm_test)
  add_dependencies(buildtests_c alloc_test)
  add_dependencies(buildtests_c alpn_test)
  add_dependencies(buildtests_c alts_counter_test)
  add_dependencies(buildtests_c alts_crypt_test)
  add_dependencies(buildtests_c alts_crypter_test)
  add_dependencies(buildtests_c alts_frame_protector_test)
  add_dependencies(buildtests_c alts_grpc_record_protocol_test)
  add_dependencies(buildtests_c alts_handshaker_client_test)
  add_dependencies(buildtests_c alts_iovec_record_protocol_test)
  add_dependencies(buildtests_c alts_security_connector_test)
  add_dependencies(buildtests_c alts_tsi_handshaker_test)
  add_dependencies(buildtests_c alts_tsi_utils_test)
  add_dependencies(buildtests_c alts_zero_copy_grpc_protector_test)
  add_dependencies(buildtests_c arena_test)
  add_dependencies(buildtests_c auth_context_test)
  add_dependencies(buildtests_c avl_test)
  add_dependencies(buildtests_c b64_test)
  add_dependencies(buildtests_c bad_server_response_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c bad_ssl_alpn_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c bad_ssl_cert_test)
  endif()
  add_dependencies(buildtests_c bin_decoder_test)
  add_dependencies(buildtests_c bin_encoder_test)
  add_dependencies(buildtests_c buffer_list_test)
  add_dependencies(buildtests_c channel_args_test)
  add_dependencies(buildtests_c channel_create_test)
  add_dependencies(buildtests_c channel_stack_builder_test)
  add_dependencies(buildtests_c channel_stack_test)
  add_dependencies(buildtests_c check_gcp_environment_linux_test)
  add_dependencies(buildtests_c check_gcp_environment_windows_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c client_ssl_test)
  endif()
  add_dependencies(buildtests_c cmdline_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c combiner_test)
  endif()
  add_dependencies(buildtests_c completion_queue_threading_test)
  add_dependencies(buildtests_c compression_test)
  add_dependencies(buildtests_c concurrent_connectivity_test)
  add_dependencies(buildtests_c connection_refused_test)
  add_dependencies(buildtests_c cpu_test)
  add_dependencies(buildtests_c dns_resolver_connectivity_using_ares_test)
  add_dependencies(buildtests_c dns_resolver_connectivity_using_native_test)
  add_dependencies(buildtests_c dns_resolver_cooldown_test)
  add_dependencies(buildtests_c dns_resolver_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c dualstack_socket_test)
  endif()
  add_dependencies(buildtests_c endpoint_pair_test)
  add_dependencies(buildtests_c env_test)
  add_dependencies(buildtests_c error_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c ev_epollex_linux_test)
  endif()
  add_dependencies(buildtests_c fake_resolver_test)
  add_dependencies(buildtests_c fake_transport_security_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c fd_conservation_posix_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c fd_posix_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c fling_stream_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c fling_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c fork_test)
  endif()
  add_dependencies(buildtests_c format_request_test)
  add_dependencies(buildtests_c frame_handler_test)
  add_dependencies(buildtests_c goaway_server_test)
  add_dependencies(buildtests_c grpc_alts_credentials_options_test)
  add_dependencies(buildtests_c grpc_byte_buffer_reader_test)
  add_dependencies(buildtests_c grpc_completion_queue_test)
  add_dependencies(buildtests_c grpc_ipv6_loopback_available_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c handshake_server_with_readahead_handshaker_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c handshake_verify_peer_options_test)
  endif()
  add_dependencies(buildtests_c histogram_test)
  add_dependencies(buildtests_c host_port_test)
  add_dependencies(buildtests_c hpack_encoder_test)
  add_dependencies(buildtests_c hpack_parser_test)
  add_dependencies(buildtests_c hpack_table_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c httpcli_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c httpscli_test)
  endif()
  add_dependencies(buildtests_c inproc_callback_test)
  add_dependencies(buildtests_c invalid_call_argument_test)
  add_dependencies(buildtests_c json_token_test)
  add_dependencies(buildtests_c jwt_verifier_test)
  add_dependencies(buildtests_c lame_client_test)
  add_dependencies(buildtests_c load_file_test)
  add_dependencies(buildtests_c manual_constructor_test)
  add_dependencies(buildtests_c message_compress_test)
  add_dependencies(buildtests_c metadata_test)
  add_dependencies(buildtests_c minimal_stack_is_minimal_test)
  add_dependencies(buildtests_c mpmcqueue_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c mpscq_test)
  endif()
  add_dependencies(buildtests_c multiple_server_queues_test)
  add_dependencies(buildtests_c murmur_hash_test)
  add_dependencies(buildtests_c no_server_test)
  add_dependencies(buildtests_c num_external_connectivity_watchers_test)
  add_dependencies(buildtests_c parse_address_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c parse_address_with_named_scope_id_test)
  endif()
  add_dependencies(buildtests_c parser_test)
  add_dependencies(buildtests_c percent_encoding_test)
  add_dependencies(buildtests_c public_headers_must_be_c89)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c resolve_address_using_ares_resolver_posix_test)
  endif()
  add_dependencies(buildtests_c resolve_address_using_ares_resolver_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c resolve_address_using_native_resolver_posix_test)
  endif()
  add_dependencies(buildtests_c resolve_address_using_native_resolver_test)
  add_dependencies(buildtests_c resource_quota_test)
  add_dependencies(buildtests_c secure_channel_create_test)
  add_dependencies(buildtests_c secure_endpoint_test)
  add_dependencies(buildtests_c security_connector_test)
  add_dependencies(buildtests_c sequential_connectivity_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c server_ssl_test)
  endif()
  add_dependencies(buildtests_c server_test)
  add_dependencies(buildtests_c slice_buffer_test)
  add_dependencies(buildtests_c slice_string_helpers_test)
  add_dependencies(buildtests_c sockaddr_resolver_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c socket_utils_test)
  endif()
  add_dependencies(buildtests_c spinlock_test)
  add_dependencies(buildtests_c ssl_credentials_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c ssl_transport_security_test)
  endif()
  add_dependencies(buildtests_c status_conversion_test)
  add_dependencies(buildtests_c stream_compression_test)
  add_dependencies(buildtests_c stream_map_test)
  add_dependencies(buildtests_c stream_owned_slice_test)
  add_dependencies(buildtests_c string_test)
  add_dependencies(buildtests_c sync_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c tcp_client_posix_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c tcp_posix_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c tcp_server_posix_test)
  endif()
  add_dependencies(buildtests_c test_core_gpr_time_test)
  add_dependencies(buildtests_c test_core_security_credentials_test)
  add_dependencies(buildtests_c test_core_slice_slice_test)
  add_dependencies(buildtests_c thd_test)
  add_dependencies(buildtests_c threadpool_test)
  add_dependencies(buildtests_c time_averaged_stats_test)
  add_dependencies(buildtests_c timeout_encoding_test)
  add_dependencies(buildtests_c timer_heap_test)
  add_dependencies(buildtests_c timer_list_test)
  add_dependencies(buildtests_c tls_test)
  add_dependencies(buildtests_c transport_security_common_api_test)
  add_dependencies(buildtests_c transport_security_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_c udp_server_test)
  endif()
  add_dependencies(buildtests_c useful_test)
  add_dependencies(buildtests_c varint_test)

  add_custom_target(buildtests_cxx)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx address_sorting_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx address_sorting_test_unsecure)
  endif()
  add_dependencies(buildtests_cxx admin_services_end2end_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx alarm_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx alts_concurrent_connectivity_test)
  endif()
  add_dependencies(buildtests_cxx alts_util_test)
  add_dependencies(buildtests_cxx async_end2end_test)
  add_dependencies(buildtests_cxx auth_property_iterator_test)
  add_dependencies(buildtests_cxx authorization_matchers_test)
  add_dependencies(buildtests_cxx aws_request_signer_test)
  add_dependencies(buildtests_cxx backoff_test)
  add_dependencies(buildtests_cxx bad_streaming_id_bad_client_test)
  add_dependencies(buildtests_cxx badreq_bad_client_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bdp_estimator_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_alarm)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_arena)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_byte_buffer)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_call_create)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_callback_streaming_ping_pong)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_callback_unary_ping_pong)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_channel)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_chttp2_hpack)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_chttp2_transport)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_closure)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_cq)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_cq_multiple_threads)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_error)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_fullstack_streaming_ping_pong)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_fullstack_streaming_pump)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_fullstack_trickle)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_fullstack_unary_ping_pong)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_metadata)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_pollset)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_threadpool)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx bm_timer)
  endif()
  add_dependencies(buildtests_cxx byte_buffer_test)
  add_dependencies(buildtests_cxx byte_stream_test)
  add_dependencies(buildtests_cxx cancel_ares_query_test)
  add_dependencies(buildtests_cxx cel_authorization_engine_test)
  add_dependencies(buildtests_cxx certificate_provider_registry_test)
  add_dependencies(buildtests_cxx certificate_provider_store_test)
  add_dependencies(buildtests_cxx cfstream_test)
  add_dependencies(buildtests_cxx channel_arguments_test)
  add_dependencies(buildtests_cxx channel_filter_test)
  add_dependencies(buildtests_cxx channel_trace_test)
  add_dependencies(buildtests_cxx channelz_registry_test)
  add_dependencies(buildtests_cxx channelz_service_test)
  add_dependencies(buildtests_cxx channelz_test)
  add_dependencies(buildtests_cxx cli_call_test)
  add_dependencies(buildtests_cxx client_callback_end2end_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx client_channel_stress_test)
  endif()
  add_dependencies(buildtests_cxx client_interceptors_end2end_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx client_lb_end2end_test)
  endif()
  add_dependencies(buildtests_cxx codegen_test_full)
  add_dependencies(buildtests_cxx codegen_test_minimal)
  add_dependencies(buildtests_cxx connection_prefix_bad_client_test)
  add_dependencies(buildtests_cxx connectivity_state_test)
  add_dependencies(buildtests_cxx context_allocator_end2end_test)
  add_dependencies(buildtests_cxx context_list_test)
  add_dependencies(buildtests_cxx delegating_channel_test)
  add_dependencies(buildtests_cxx destroy_grpclb_channel_with_active_connect_stress_test)
  add_dependencies(buildtests_cxx dual_ref_counted_test)
  add_dependencies(buildtests_cxx duplicate_header_bad_client_test)
  add_dependencies(buildtests_cxx end2end_test)
  add_dependencies(buildtests_cxx error_details_test)
  add_dependencies(buildtests_cxx error_utils_test)
  add_dependencies(buildtests_cxx evaluate_args_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx examine_stack_test)
  endif()
  add_dependencies(buildtests_cxx exception_test)
  add_dependencies(buildtests_cxx file_watcher_certificate_provider_factory_test)
  add_dependencies(buildtests_cxx filter_end2end_test)
  add_dependencies(buildtests_cxx flaky_network_test)
  add_dependencies(buildtests_cxx generic_end2end_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx global_config_env_test)
  endif()
  add_dependencies(buildtests_cxx global_config_test)
  add_dependencies(buildtests_cxx google_mesh_ca_certificate_provider_factory_test)
  add_dependencies(buildtests_cxx grpc_authorization_engine_test)
  add_dependencies(buildtests_cxx grpc_cli)
  add_dependencies(buildtests_cxx grpc_tls_certificate_distributor_test)
  add_dependencies(buildtests_cxx grpc_tls_certificate_provider_test)
  add_dependencies(buildtests_cxx grpc_tls_credentials_options_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx grpc_tool_test)
  endif()
  add_dependencies(buildtests_cxx grpclb_api_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx grpclb_end2end_test)
  endif()
  add_dependencies(buildtests_cxx h2_ssl_session_reuse_test)
  add_dependencies(buildtests_cxx head_of_line_blocking_bad_client_test)
  add_dependencies(buildtests_cxx headers_bad_client_test)
  add_dependencies(buildtests_cxx health_service_end2end_test)
  add_dependencies(buildtests_cxx http2_client)
  add_dependencies(buildtests_cxx hybrid_end2end_test)
  add_dependencies(buildtests_cxx init_test)
  add_dependencies(buildtests_cxx initial_settings_frame_bad_client_test)
  add_dependencies(buildtests_cxx insecure_security_connector_test)
  add_dependencies(buildtests_cxx interop_client)
  add_dependencies(buildtests_cxx interop_server)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx interop_test)
  endif()
  add_dependencies(buildtests_cxx json_test)
  add_dependencies(buildtests_cxx large_metadata_bad_client_test)
  add_dependencies(buildtests_cxx lb_get_cpu_stats_test)
  add_dependencies(buildtests_cxx lb_load_data_store_test)
  add_dependencies(buildtests_cxx linux_system_roots_test)
  add_dependencies(buildtests_cxx log_test)
  add_dependencies(buildtests_cxx matchers_test)
  add_dependencies(buildtests_cxx message_allocator_end2end_test)
  add_dependencies(buildtests_cxx mock_stream_test)
  add_dependencies(buildtests_cxx mock_test)
  add_dependencies(buildtests_cxx nonblocking_test)
  add_dependencies(buildtests_cxx noop-benchmark)
  add_dependencies(buildtests_cxx orphanable_test)
  add_dependencies(buildtests_cxx out_of_bounds_bad_client_test)
  add_dependencies(buildtests_cxx pid_controller_test)
  add_dependencies(buildtests_cxx port_sharing_end2end_test)
  add_dependencies(buildtests_cxx proto_server_reflection_test)
  add_dependencies(buildtests_cxx proto_utils_test)
  add_dependencies(buildtests_cxx qps_json_driver)
  add_dependencies(buildtests_cxx qps_worker)
  add_dependencies(buildtests_cxx raw_end2end_test)
  add_dependencies(buildtests_cxx rbac_translator_test)
  add_dependencies(buildtests_cxx ref_counted_ptr_test)
  add_dependencies(buildtests_cxx ref_counted_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx remove_stream_from_stalled_lists_test)
  endif()
  add_dependencies(buildtests_cxx retry_throttle_test)
  add_dependencies(buildtests_cxx secure_auth_context_test)
  add_dependencies(buildtests_cxx server_builder_plugin_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx server_builder_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx server_builder_with_socket_mutator_test)
  endif()
  add_dependencies(buildtests_cxx server_chttp2_test)
  add_dependencies(buildtests_cxx server_context_test_spouse_test)
  add_dependencies(buildtests_cxx server_early_return_test)
  add_dependencies(buildtests_cxx server_interceptors_end2end_test)
  add_dependencies(buildtests_cxx server_registered_method_bad_client_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx server_request_call_test)
  endif()
  add_dependencies(buildtests_cxx service_config_end2end_test)
  add_dependencies(buildtests_cxx service_config_test)
  add_dependencies(buildtests_cxx settings_timeout_test)
  add_dependencies(buildtests_cxx shutdown_test)
  add_dependencies(buildtests_cxx simple_request_bad_client_test)
  add_dependencies(buildtests_cxx sockaddr_utils_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx stack_tracer_test)
  endif()
  add_dependencies(buildtests_cxx stat_test)
  add_dependencies(buildtests_cxx static_metadata_test)
  add_dependencies(buildtests_cxx stats_test)
  add_dependencies(buildtests_cxx status_helper_test)
  add_dependencies(buildtests_cxx status_metadata_test)
  add_dependencies(buildtests_cxx status_util_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx stranded_event_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx streaming_throughput_test)
  endif()
  add_dependencies(buildtests_cxx string_ref_test)
  add_dependencies(buildtests_cxx test_cpp_client_credentials_test)
  add_dependencies(buildtests_cxx test_cpp_server_credentials_test)
  add_dependencies(buildtests_cxx test_cpp_util_slice_test)
  add_dependencies(buildtests_cxx test_cpp_util_time_test)
  add_dependencies(buildtests_cxx thread_manager_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx thread_stress_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx time_jump_test)
  endif()
  add_dependencies(buildtests_cxx time_util_test)
  add_dependencies(buildtests_cxx timer_test)
  add_dependencies(buildtests_cxx tls_security_connector_test)
  add_dependencies(buildtests_cxx too_many_pings_test)
  add_dependencies(buildtests_cxx unknown_frame_bad_client_test)
  add_dependencies(buildtests_cxx uri_parser_test)
  add_dependencies(buildtests_cxx window_overflow_bad_client_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx work_serializer_test)
  endif()
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx writes_per_rpc_test)
  endif()
  add_dependencies(buildtests_cxx xds_bootstrap_test)
  add_dependencies(buildtests_cxx xds_certificate_provider_test)
  add_dependencies(buildtests_cxx xds_credentials_end2end_test)
  add_dependencies(buildtests_cxx xds_credentials_test)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
    add_dependencies(buildtests_cxx xds_end2end_test)
  endif()
  add_dependencies(buildtests_cxx xds_interop_client)
  add_dependencies(buildtests_cxx xds_interop_server)
  add_dependencies(buildtests_cxx alts_credentials_fuzzer_one_entry)
  add_dependencies(buildtests_cxx client_fuzzer_one_entry)
  add_dependencies(buildtests_cxx hpack_parser_fuzzer_test_one_entry)
  add_dependencies(buildtests_cxx http_request_fuzzer_test_one_entry)
  add_dependencies(buildtests_cxx http_response_fuzzer_test_one_entry)
  add_dependencies(buildtests_cxx json_fuzzer_test_one_entry)
  add_dependencies(buildtests_cxx nanopb_fuzzer_response_test_one_entry)
  add_dependencies(buildtests_cxx nanopb_fuzzer_serverlist_test_one_entry)
  add_dependencies(buildtests_cxx percent_decode_fuzzer_one_entry)
  add_dependencies(buildtests_cxx percent_encode_fuzzer_one_entry)
  add_dependencies(buildtests_cxx server_fuzzer_one_entry)
  add_dependencies(buildtests_cxx ssl_server_fuzzer_one_entry)
  add_dependencies(buildtests_cxx uri_fuzzer_test_one_entry)

  add_custom_target(buildtests
    DEPENDS buildtests_c buildtests_cxx)
endif()


add_library(address_sorting
  third_party/address_sorting/address_sorting.c
  third_party/address_sorting/address_sorting_posix.c
  third_party/address_sorting/address_sorting_windows.c
)

set_target_properties(address_sorting PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(address_sorting PROPERTIES COMPILE_PDB_NAME "address_sorting"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/address_sorting.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(address_sorting
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(address_sorting
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS address_sorting EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_TESTS)

add_library(end2end_nosec_tests
  test/core/end2end/cq_verifier.cc
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/core/end2end/end2end_nosec_tests.cc
  test/core/end2end/end2end_test_utils.cc
  test/core/end2end/fixtures/http_proxy_fixture.cc
  test/core/end2end/fixtures/local_util.cc
  test/core/end2end/fixtures/proxy.cc
  test/core/end2end/tests/authority_not_supported.cc
  test/core/end2end/tests/bad_hostname.cc
  test/core/end2end/tests/bad_ping.cc
  test/core/end2end/tests/binary_metadata.cc
  test/core/end2end/tests/call_host_override.cc
  test/core/end2end/tests/cancel_after_accept.cc
  test/core/end2end/tests/cancel_after_client_done.cc
  test/core/end2end/tests/cancel_after_invoke.cc
  test/core/end2end/tests/cancel_after_round_trip.cc
  test/core/end2end/tests/cancel_before_invoke.cc
  test/core/end2end/tests/cancel_in_a_vacuum.cc
  test/core/end2end/tests/cancel_with_status.cc
  test/core/end2end/tests/channelz.cc
  test/core/end2end/tests/client_streaming.cc
  test/core/end2end/tests/compressed_payload.cc
  test/core/end2end/tests/connectivity.cc
  test/core/end2end/tests/default_host.cc
  test/core/end2end/tests/disappearing_server.cc
  test/core/end2end/tests/empty_batch.cc
  test/core/end2end/tests/filter_causes_close.cc
  test/core/end2end/tests/filter_context.cc
  test/core/end2end/tests/filter_init_fails.cc
  test/core/end2end/tests/filter_latency.cc
  test/core/end2end/tests/filter_status_code.cc
  test/core/end2end/tests/graceful_server_shutdown.cc
  test/core/end2end/tests/high_initial_seqno.cc
  test/core/end2end/tests/hpack_size.cc
  test/core/end2end/tests/idempotent_request.cc
  test/core/end2end/tests/invoke_large_request.cc
  test/core/end2end/tests/keepalive_timeout.cc
  test/core/end2end/tests/large_metadata.cc
  test/core/end2end/tests/max_concurrent_streams.cc
  test/core/end2end/tests/max_connection_age.cc
  test/core/end2end/tests/max_connection_idle.cc
  test/core/end2end/tests/max_message_length.cc
  test/core/end2end/tests/negative_deadline.cc
  test/core/end2end/tests/no_error_on_hotpath.cc
  test/core/end2end/tests/no_logging.cc
  test/core/end2end/tests/no_op.cc
  test/core/end2end/tests/payload.cc
  test/core/end2end/tests/ping.cc
  test/core/end2end/tests/ping_pong_streaming.cc
  test/core/end2end/tests/proxy_auth.cc
  test/core/end2end/tests/registered_call.cc
  test/core/end2end/tests/request_with_flags.cc
  test/core/end2end/tests/request_with_payload.cc
  test/core/end2end/tests/resource_quota_server.cc
  test/core/end2end/tests/retry.cc
  test/core/end2end/tests/retry_cancel_during_delay.cc
  test/core/end2end/tests/retry_cancellation.cc
  test/core/end2end/tests/retry_disabled.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_initial_batch.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_subsequent_batch.cc
  test/core/end2end/tests/retry_lb_drop.cc
  test/core/end2end/tests/retry_non_retriable_status.cc
  test/core/end2end/tests/retry_non_retriable_status_before_recv_trailing_metadata_started.cc
  test/core/end2end/tests/retry_recv_initial_metadata.cc
  test/core/end2end/tests/retry_recv_message.cc
  test/core/end2end/tests/retry_server_pushback_delay.cc
  test/core/end2end/tests/retry_server_pushback_disabled.cc
  test/core/end2end/tests/retry_streaming.cc
  test/core/end2end/tests/retry_streaming_after_commit.cc
  test/core/end2end/tests/retry_streaming_succeeds_before_replay_finished.cc
  test/core/end2end/tests/retry_throttled.cc
  test/core/end2end/tests/retry_too_many_attempts.cc
  test/core/end2end/tests/server_finishes_request.cc
  test/core/end2end/tests/server_streaming.cc
  test/core/end2end/tests/shutdown_finishes_calls.cc
  test/core/end2end/tests/shutdown_finishes_tags.cc
  test/core/end2end/tests/simple_cacheable_request.cc
  test/core/end2end/tests/simple_delayed_request.cc
  test/core/end2end/tests/simple_metadata.cc
  test/core/end2end/tests/simple_request.cc
  test/core/end2end/tests/stream_compression_compressed_payload.cc
  test/core/end2end/tests/stream_compression_payload.cc
  test/core/end2end/tests/stream_compression_ping_pong_streaming.cc
  test/core/end2end/tests/streaming_error_response.cc
  test/core/end2end/tests/trailing_metadata.cc
  test/core/end2end/tests/workaround_cronet_compression.cc
  test/core/end2end/tests/write_buffering.cc
  test/core/end2end/tests/write_buffering_at_end.cc
  test/core/util/test_lb_policies.cc
)

set_target_properties(end2end_nosec_tests PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(end2end_nosec_tests PROPERTIES COMPILE_PDB_NAME "end2end_nosec_tests"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/end2end_nosec_tests.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(end2end_nosec_tests
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(end2end_nosec_tests
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_library(end2end_tests
  test/core/end2end/cq_verifier.cc
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/core/end2end/end2end_test_utils.cc
  test/core/end2end/end2end_tests.cc
  test/core/end2end/fixtures/http_proxy_fixture.cc
  test/core/end2end/fixtures/local_util.cc
  test/core/end2end/fixtures/proxy.cc
  test/core/end2end/tests/authority_not_supported.cc
  test/core/end2end/tests/bad_hostname.cc
  test/core/end2end/tests/bad_ping.cc
  test/core/end2end/tests/binary_metadata.cc
  test/core/end2end/tests/call_creds.cc
  test/core/end2end/tests/call_host_override.cc
  test/core/end2end/tests/cancel_after_accept.cc
  test/core/end2end/tests/cancel_after_client_done.cc
  test/core/end2end/tests/cancel_after_invoke.cc
  test/core/end2end/tests/cancel_after_round_trip.cc
  test/core/end2end/tests/cancel_before_invoke.cc
  test/core/end2end/tests/cancel_in_a_vacuum.cc
  test/core/end2end/tests/cancel_with_status.cc
  test/core/end2end/tests/channelz.cc
  test/core/end2end/tests/client_streaming.cc
  test/core/end2end/tests/compressed_payload.cc
  test/core/end2end/tests/connectivity.cc
  test/core/end2end/tests/default_host.cc
  test/core/end2end/tests/disappearing_server.cc
  test/core/end2end/tests/empty_batch.cc
  test/core/end2end/tests/filter_causes_close.cc
  test/core/end2end/tests/filter_context.cc
  test/core/end2end/tests/filter_init_fails.cc
  test/core/end2end/tests/filter_latency.cc
  test/core/end2end/tests/filter_status_code.cc
  test/core/end2end/tests/graceful_server_shutdown.cc
  test/core/end2end/tests/high_initial_seqno.cc
  test/core/end2end/tests/hpack_size.cc
  test/core/end2end/tests/idempotent_request.cc
  test/core/end2end/tests/invoke_large_request.cc
  test/core/end2end/tests/keepalive_timeout.cc
  test/core/end2end/tests/large_metadata.cc
  test/core/end2end/tests/max_concurrent_streams.cc
  test/core/end2end/tests/max_connection_age.cc
  test/core/end2end/tests/max_connection_idle.cc
  test/core/end2end/tests/max_message_length.cc
  test/core/end2end/tests/negative_deadline.cc
  test/core/end2end/tests/no_error_on_hotpath.cc
  test/core/end2end/tests/no_logging.cc
  test/core/end2end/tests/no_op.cc
  test/core/end2end/tests/payload.cc
  test/core/end2end/tests/ping.cc
  test/core/end2end/tests/ping_pong_streaming.cc
  test/core/end2end/tests/proxy_auth.cc
  test/core/end2end/tests/registered_call.cc
  test/core/end2end/tests/request_with_flags.cc
  test/core/end2end/tests/request_with_payload.cc
  test/core/end2end/tests/resource_quota_server.cc
  test/core/end2end/tests/retry.cc
  test/core/end2end/tests/retry_cancel_during_delay.cc
  test/core/end2end/tests/retry_cancellation.cc
  test/core/end2end/tests/retry_disabled.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_initial_batch.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_subsequent_batch.cc
  test/core/end2end/tests/retry_lb_drop.cc
  test/core/end2end/tests/retry_non_retriable_status.cc
  test/core/end2end/tests/retry_non_retriable_status_before_recv_trailing_metadata_started.cc
  test/core/end2end/tests/retry_recv_initial_metadata.cc
  test/core/end2end/tests/retry_recv_message.cc
  test/core/end2end/tests/retry_server_pushback_delay.cc
  test/core/end2end/tests/retry_server_pushback_disabled.cc
  test/core/end2end/tests/retry_streaming.cc
  test/core/end2end/tests/retry_streaming_after_commit.cc
  test/core/end2end/tests/retry_streaming_succeeds_before_replay_finished.cc
  test/core/end2end/tests/retry_throttled.cc
  test/core/end2end/tests/retry_too_many_attempts.cc
  test/core/end2end/tests/server_finishes_request.cc
  test/core/end2end/tests/server_streaming.cc
  test/core/end2end/tests/shutdown_finishes_calls.cc
  test/core/end2end/tests/shutdown_finishes_tags.cc
  test/core/end2end/tests/simple_cacheable_request.cc
  test/core/end2end/tests/simple_delayed_request.cc
  test/core/end2end/tests/simple_metadata.cc
  test/core/end2end/tests/simple_request.cc
  test/core/end2end/tests/stream_compression_compressed_payload.cc
  test/core/end2end/tests/stream_compression_payload.cc
  test/core/end2end/tests/stream_compression_ping_pong_streaming.cc
  test/core/end2end/tests/streaming_error_response.cc
  test/core/end2end/tests/trailing_metadata.cc
  test/core/end2end/tests/workaround_cronet_compression.cc
  test/core/end2end/tests/write_buffering.cc
  test/core/end2end/tests/write_buffering_at_end.cc
  test/core/util/test_lb_policies.cc
)

set_target_properties(end2end_tests PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(end2end_tests PROPERTIES COMPILE_PDB_NAME "end2end_tests"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/end2end_tests.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(end2end_tests
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(end2end_tests
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()

add_library(gpr
  src/core/ext/upb-generated/google/api/annotations.upb.c
  src/core/ext/upb-generated/google/api/expr/v1alpha1/checked.upb.c
  src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.c
  src/core/ext/upb-generated/google/api/http.upb.c
  src/core/ext/upb-generated/google/protobuf/any.upb.c
  src/core/ext/upb-generated/google/protobuf/duration.upb.c
  src/core/ext/upb-generated/google/protobuf/empty.upb.c
  src/core/ext/upb-generated/google/protobuf/struct.upb.c
  src/core/ext/upb-generated/google/protobuf/timestamp.upb.c
  src/core/ext/upb-generated/google/protobuf/wrappers.upb.c
  src/core/ext/upb-generated/google/rpc/status.upb.c
  src/core/lib/gpr/alloc.cc
  src/core/lib/gpr/atm.cc
  src/core/lib/gpr/cpu_iphone.cc
  src/core/lib/gpr/cpu_linux.cc
  src/core/lib/gpr/cpu_posix.cc
  src/core/lib/gpr/cpu_windows.cc
  src/core/lib/gpr/env_linux.cc
  src/core/lib/gpr/env_posix.cc
  src/core/lib/gpr/env_windows.cc
  src/core/lib/gpr/log.cc
  src/core/lib/gpr/log_android.cc
  src/core/lib/gpr/log_linux.cc
  src/core/lib/gpr/log_posix.cc
  src/core/lib/gpr/log_windows.cc
  src/core/lib/gpr/murmur_hash.cc
  src/core/lib/gpr/string.cc
  src/core/lib/gpr/string_posix.cc
  src/core/lib/gpr/string_util_windows.cc
  src/core/lib/gpr/string_windows.cc
  src/core/lib/gpr/sync.cc
  src/core/lib/gpr/sync_abseil.cc
  src/core/lib/gpr/sync_posix.cc
  src/core/lib/gpr/sync_windows.cc
  src/core/lib/gpr/time.cc
  src/core/lib/gpr/time_posix.cc
  src/core/lib/gpr/time_precise.cc
  src/core/lib/gpr/time_windows.cc
  src/core/lib/gpr/tls_pthread.cc
  src/core/lib/gpr/tmpfile_msys.cc
  src/core/lib/gpr/tmpfile_posix.cc
  src/core/lib/gpr/tmpfile_windows.cc
  src/core/lib/gpr/wrap_memcpy.cc
  src/core/lib/gprpp/arena.cc
  src/core/lib/gprpp/examine_stack.cc
  src/core/lib/gprpp/fork.cc
  src/core/lib/gprpp/global_config_env.cc
  src/core/lib/gprpp/host_port.cc
  src/core/lib/gprpp/mpscq.cc
  src/core/lib/gprpp/stat_posix.cc
  src/core/lib/gprpp/stat_windows.cc
  src/core/lib/gprpp/status_helper.cc
  src/core/lib/gprpp/thd_posix.cc
  src/core/lib/gprpp/thd_windows.cc
  src/core/lib/gprpp/time_util.cc
  src/core/lib/profiling/basic_timers.cc
  src/core/lib/profiling/stap_timers.cc
)

set_target_properties(gpr PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(gpr PROPERTIES COMPILE_PDB_NAME "gpr"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpr.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(gpr
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(gpr
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::base
  absl::memory
  absl::status
  absl::str_format
  absl::strings
  absl::synchronization
  absl::time
  absl::optional
  upb
)
if(_gRPC_PLATFORM_ANDROID)
  target_link_libraries(gpr
    android
    log
  )
endif()

foreach(_hdr
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_abseil.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/support/alloc.h
  include/grpc/support/atm.h
  include/grpc/support/atm_gcc_atomic.h
  include/grpc/support/atm_gcc_sync.h
  include/grpc/support/atm_windows.h
  include/grpc/support/cpu.h
  include/grpc/support/log.h
  include/grpc/support/log_windows.h
  include/grpc/support/port_platform.h
  include/grpc/support/string_util.h
  include/grpc/support/sync.h
  include/grpc/support/sync_abseil.h
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS gpr EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc
  src/core/ext/filters/census/grpc_context.cc
  src/core/ext/filters/client_channel/backend_metric.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/config_selector.cc
  src/core/ext/filters/client_channel/dynamic_filters.cc
  src/core/ext/filters/client_channel/global_subchannel_pool.cc
  src/core/ext/filters/client_channel/health/health_check_client.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy/address_filtering.cc
  src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc
  src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc
  src/core/ext/filters/client_channel/lb_policy/priority/priority.cc
  src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.cc
  src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc
  src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc
  src/core/ext/filters/client_channel/lb_policy/xds/cds.cc
  src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_impl.cc
  src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_manager.cc
  src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_resolver.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/local_subchannel_pool.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_libuv.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_libuv.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.cc
  src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  src/core/ext/filters/client_channel/resolver/google_c2p/google_c2p_resolver.cc
  src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc
  src/core/ext/filters/client_channel/resolver/xds/xds_resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/resolver_result_parsing.cc
  src/core/ext/filters/client_channel/retry_filter.cc
  src/core/ext/filters/client_channel/retry_service_config.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/server_address.cc
  src/core/ext/filters/client_channel/service_config.cc
  src/core/ext/filters/client_channel/service_config_channel_arg_filter.cc
  src/core/ext/filters/client_channel/service_config_parser.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_pool_interface.cc
  src/core/ext/filters/client_idle/client_idle_filter.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_filter.cc
  src/core/ext/filters/fault_injection/service_config_parser.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/message_compress/message_decompress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/max_age/max_age_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc
  src/core/ext/filters/workarounds/workaround_utils.cc
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc
  src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  src/core/ext/transport/chttp2/transport/context_list.cc
  src/core/ext/transport/chttp2/transport/flow_control.cc
  src/core/ext/transport/chttp2/transport/frame_data.cc
  src/core/ext/transport/chttp2/transport/frame_goaway.cc
  src/core/ext/transport/chttp2/transport/frame_ping.cc
  src/core/ext/transport/chttp2/transport/frame_rst_stream.cc
  src/core/ext/transport/chttp2/transport/frame_settings.cc
  src/core/ext/transport/chttp2/transport/frame_window_update.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder.cc
  src/core/ext/transport/chttp2/transport/hpack_parser.cc
  src/core/ext/transport/chttp2/transport/hpack_table.cc
  src/core/ext/transport/chttp2/transport/http2_settings.cc
  src/core/ext/transport/chttp2/transport/huffsyms.cc
  src/core/ext/transport/chttp2/transport/incoming_metadata.cc
  src/core/ext/transport/chttp2/transport/parsing.cc
  src/core/ext/transport/chttp2/transport/stream_lists.cc
  src/core/ext/transport/chttp2/transport/stream_map.cc
  src/core/ext/transport/chttp2/transport/varint.cc
  src/core/ext/transport/chttp2/transport/writing.cc
  src/core/ext/transport/inproc/inproc_plugin.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/upb-generated/envoy/admin/v3/config_dump.upb.c
  src/core/ext/upb-generated/envoy/annotations/deprecation.upb.c
  src/core/ext/upb-generated/envoy/annotations/resource.upb.c
  src/core/ext/upb-generated/envoy/config/accesslog/v3/accesslog.upb.c
  src/core/ext/upb-generated/envoy/config/bootstrap/v3/bootstrap.upb.c
  src/core/ext/upb-generated/envoy/config/cluster/v3/circuit_breaker.upb.c
  src/core/ext/upb-generated/envoy/config/cluster/v3/cluster.upb.c
  src/core/ext/upb-generated/envoy/config/cluster/v3/filter.upb.c
  src/core/ext/upb-generated/envoy/config/cluster/v3/outlier_detection.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/address.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/backoff.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/base.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/config_source.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/event_service_config.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/extension.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/grpc_service.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/health_check.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/http_uri.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/protocol.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/proxy_protocol.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/socket_option.upb.c
  src/core/ext/upb-generated/envoy/config/core/v3/substitution_format_string.upb.c
  src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint.upb.c
  src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint_components.upb.c
  src/core/ext/upb-generated/envoy/config/endpoint/v3/load_report.upb.c
  src/core/ext/upb-generated/envoy/config/listener/v3/api_listener.upb.c
  src/core/ext/upb-generated/envoy/config/listener/v3/listener.upb.c
  src/core/ext/upb-generated/envoy/config/listener/v3/listener_components.upb.c
  src/core/ext/upb-generated/envoy/config/listener/v3/udp_listener_config.upb.c
  src/core/ext/upb-generated/envoy/config/metrics/v3/stats.upb.c
  src/core/ext/upb-generated/envoy/config/overload/v3/overload.upb.c
  src/core/ext/upb-generated/envoy/config/rbac/v3/rbac.upb.c
  src/core/ext/upb-generated/envoy/config/route/v3/route.upb.c
  src/core/ext/upb-generated/envoy/config/route/v3/route_components.upb.c
  src/core/ext/upb-generated/envoy/config/route/v3/scoped_route.upb.c
  src/core/ext/upb-generated/envoy/config/trace/v3/http_tracer.upb.c
  src/core/ext/upb-generated/envoy/extensions/clusters/aggregate/v3/cluster.upb.c
  src/core/ext/upb-generated/envoy/extensions/filters/common/fault/v3/fault.upb.c
  src/core/ext/upb-generated/envoy/extensions/filters/http/fault/v3/fault.upb.c
  src/core/ext/upb-generated/envoy/extensions/filters/http/router/v3/router.upb.c
  src/core/ext/upb-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.c
  src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/cert.upb.c
  src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/common.upb.c
  src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/secret.upb.c
  src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/tls.upb.c
  src/core/ext/upb-generated/envoy/service/cluster/v3/cds.upb.c
  src/core/ext/upb-generated/envoy/service/discovery/v3/ads.upb.c
  src/core/ext/upb-generated/envoy/service/discovery/v3/discovery.upb.c
  src/core/ext/upb-generated/envoy/service/endpoint/v3/eds.upb.c
  src/core/ext/upb-generated/envoy/service/listener/v3/lds.upb.c
  src/core/ext/upb-generated/envoy/service/load_stats/v3/lrs.upb.c
  src/core/ext/upb-generated/envoy/service/route/v3/rds.upb.c
  src/core/ext/upb-generated/envoy/service/route/v3/srds.upb.c
  src/core/ext/upb-generated/envoy/service/status/v3/csds.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/metadata.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/node.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/number.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/path.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/regex.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/string.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/struct.upb.c
  src/core/ext/upb-generated/envoy/type/matcher/v3/value.upb.c
  src/core/ext/upb-generated/envoy/type/metadata/v3/metadata.upb.c
  src/core/ext/upb-generated/envoy/type/tracing/v3/custom_tag.upb.c
  src/core/ext/upb-generated/envoy/type/v3/http.upb.c
  src/core/ext/upb-generated/envoy/type/v3/percent.upb.c
  src/core/ext/upb-generated/envoy/type/v3/range.upb.c
  src/core/ext/upb-generated/envoy/type/v3/semantic_version.upb.c
  src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.c
  src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.c
  src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.c
  src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.c
  src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.c
  src/core/ext/upb-generated/udpa/annotations/migrate.upb.c
  src/core/ext/upb-generated/udpa/annotations/security.upb.c
  src/core/ext/upb-generated/udpa/annotations/sensitive.upb.c
  src/core/ext/upb-generated/udpa/annotations/status.upb.c
  src/core/ext/upb-generated/udpa/annotations/versioning.upb.c
  src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.c
  src/core/ext/upb-generated/udpa/type/v1/typed_struct.upb.c
  src/core/ext/upb-generated/validate/validate.upb.c
  src/core/ext/upb-generated/xds/core/v3/authority.upb.c
  src/core/ext/upb-generated/xds/core/v3/collection_entry.upb.c
  src/core/ext/upb-generated/xds/core/v3/context_params.upb.c
  src/core/ext/upb-generated/xds/core/v3/resource.upb.c
  src/core/ext/upb-generated/xds/core/v3/resource_locator.upb.c
  src/core/ext/upb-generated/xds/core/v3/resource_name.upb.c
  src/core/ext/upbdefs-generated/envoy/admin/v3/config_dump.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/annotations/deprecation.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/annotations/resource.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/accesslog/v3/accesslog.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/bootstrap/v3/bootstrap.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/cluster/v3/circuit_breaker.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/cluster/v3/cluster.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/cluster/v3/filter.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/cluster/v3/outlier_detection.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/address.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/backoff.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/base.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/config_source.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/event_service_config.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/extension.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/grpc_service.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/health_check.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/http_uri.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/protocol.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/proxy_protocol.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/socket_option.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/core/v3/substitution_format_string.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint_components.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/load_report.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/listener/v3/api_listener.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener_components.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/listener/v3/udp_listener_config.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/metrics/v3/stats.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/overload/v3/overload.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/route/v3/route.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/route/v3/route_components.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/route/v3/scoped_route.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/config/trace/v3/http_tracer.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/filters/common/fault/v3/fault.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/filters/http/fault/v3/fault.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/filters/http/router/v3/router.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/cert.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/common.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/secret.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/cluster/v3/cds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/discovery/v3/ads.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/discovery/v3/discovery.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/endpoint/v3/eds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/listener/v3/lds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/load_stats/v3/lrs.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/route/v3/rds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/route/v3/srds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/service/status/v3/csds.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/metadata.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/node.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/number.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/path.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/regex.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/string.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/struct.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/matcher/v3/value.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/metadata/v3/metadata.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/tracing/v3/custom_tag.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/v3/http.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/v3/percent.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/v3/range.upbdefs.c
  src/core/ext/upbdefs-generated/envoy/type/v3/semantic_version.upbdefs.c
  src/core/ext/upbdefs-generated/google/api/annotations.upbdefs.c
  src/core/ext/upbdefs-generated/google/api/http.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/any.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/duration.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/empty.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/struct.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/timestamp.upbdefs.c
  src/core/ext/upbdefs-generated/google/protobuf/wrappers.upbdefs.c
  src/core/ext/upbdefs-generated/google/rpc/status.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/annotations/migrate.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/annotations/security.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/annotations/sensitive.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/annotations/status.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/annotations/versioning.upbdefs.c
  src/core/ext/upbdefs-generated/udpa/type/v1/typed_struct.upbdefs.c
  src/core/ext/upbdefs-generated/validate/validate.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/authority.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/collection_entry.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/context_params.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/resource.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/resource_locator.upbdefs.c
  src/core/ext/upbdefs-generated/xds/core/v3/resource_name.upbdefs.c
  src/core/ext/xds/certificate_provider_registry.cc
  src/core/ext/xds/certificate_provider_store.cc
  src/core/ext/xds/file_watcher_certificate_provider_factory.cc
  src/core/ext/xds/xds_api.cc
  src/core/ext/xds/xds_bootstrap.cc
  src/core/ext/xds/xds_certificate_provider.cc
  src/core/ext/xds/xds_client.cc
  src/core/ext/xds/xds_client_stats.cc
  src/core/ext/xds/xds_http_fault_filter.cc
  src/core/ext/xds/xds_http_filters.cc
  src/core/ext/xds/xds_server_config_fetcher.cc
  src/core/lib/address_utils/parse_address.cc
  src/core/lib/address_utils/sockaddr_utils.cc
  src/core/lib/avl/avl.cc
  src/core/lib/backoff/backoff.cc
  src/core/lib/channel/channel_args.cc
  src/core/lib/channel/channel_stack.cc
  src/core/lib/channel/channel_stack_builder.cc
  src/core/lib/channel/channel_trace.cc
  src/core/lib/channel/channelz.cc
  src/core/lib/channel/channelz_registry.cc
  src/core/lib/channel/connected_channel.cc
  src/core/lib/channel/handshaker.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_args.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/debug/trace.cc
  src/core/lib/event_engine/slice_allocator.cc
  src/core/lib/event_engine/sockaddr.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/httpcli_security_connector.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/buffer_list.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/cfstream_handle.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/dualstack_socket_posix.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_cfstream.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/error_cfstream.cc
  src/core/lib/iomgr/ev_apple.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/executor/mpmcqueue.cc
  src/core/lib/iomgr/executor/threadpool.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/grpc_if_nametoindex_posix.cc
  src/core/lib/iomgr/grpc_if_nametoindex_unsupported.cc
  src/core/lib/iomgr/internal_errqueue.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_posix_cfstream.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/polling_entity.cc
  src/core/lib/iomgr/pollset.cc
  src/core/lib/iomgr/pollset_custom.cc
  src/core/lib/iomgr/pollset_set.cc
  src/core/lib/iomgr/pollset_set_custom.cc
  src/core/lib/iomgr/pollset_set_windows.cc
  src/core/lib/iomgr/pollset_uv.cc
  src/core/lib/iomgr/pollset_windows.cc
  src/core/lib/iomgr/resolve_address.cc
  src/core/lib/iomgr/resolve_address_custom.cc
  src/core/lib/iomgr/resolve_address_posix.cc
  src/core/lib/iomgr/resolve_address_windows.cc
  src/core/lib/iomgr/resource_quota.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
  src/core/lib/iomgr/tcp_client_cfstream.cc
  src/core/lib/iomgr/tcp_client_custom.cc
  src/core/lib/iomgr/tcp_client_posix.cc
  src/core/lib/iomgr/tcp_client_windows.cc
  src/core/lib/iomgr/tcp_custom.cc
  src/core/lib/iomgr/tcp_posix.cc
  src/core/lib/iomgr/tcp_server.cc
  src/core/lib/iomgr/tcp_server_custom.cc
  src/core/lib/iomgr/tcp_server_posix.cc
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc
  src/core/lib/iomgr/tcp_server_windows.cc
  src/core/lib/iomgr/tcp_uv.cc
  src/core/lib/iomgr/tcp_windows.cc
  src/core/lib/iomgr/time_averaged_stats.cc
  src/core/lib/iomgr/timer.cc
  src/core/lib/iomgr/timer_custom.cc
  src/core/lib/iomgr/timer_generic.cc
  src/core/lib/iomgr/timer_heap.cc
  src/core/lib/iomgr/timer_manager.cc
  src/core/lib/iomgr/timer_uv.cc
  src/core/lib/iomgr/udp_server.cc
  src/core/lib/iomgr/unix_sockets_posix.cc
  src/core/lib/iomgr/unix_sockets_posix_noop.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/iomgr/work_serializer.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_util.cc
  src/core/lib/json/json_writer.cc
  src/core/lib/matchers/matchers.cc
  src/core/lib/security/context/security_context.cc
  src/core/lib/security/credentials/alts/alts_credentials.cc
  src/core/lib/security/credentials/alts/check_gcp_environment.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc
  src/core/lib/security/credentials/composite/composite_credentials.cc
  src/core/lib/security/credentials/credentials.cc
  src/core/lib/security/credentials/credentials_metadata.cc
  src/core/lib/security/credentials/external/aws_external_account_credentials.cc
  src/core/lib/security/credentials/external/aws_request_signer.cc
  src/core/lib/security/credentials/external/external_account_credentials.cc
  src/core/lib/security/credentials/external/file_external_account_credentials.cc
  src/core/lib/security/credentials/external/url_external_account_credentials.cc
  src/core/lib/security/credentials/fake/fake_credentials.cc
  src/core/lib/security/credentials/google_default/credentials_generic.cc
  src/core/lib/security/credentials/google_default/google_default_credentials.cc
  src/core/lib/security/credentials/iam/iam_credentials.cc
  src/core/lib/security/credentials/insecure/insecure_credentials.cc
  src/core/lib/security/credentials/jwt/json_token.cc
  src/core/lib/security/credentials/jwt/jwt_credentials.cc
  src/core/lib/security/credentials/jwt/jwt_verifier.cc
  src/core/lib/security/credentials/local/local_credentials.cc
  src/core/lib/security/credentials/oauth2/oauth2_credentials.cc
  src/core/lib/security/credentials/plugin/plugin_credentials.cc
  src/core/lib/security/credentials/ssl/ssl_credentials.cc
  src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.cc
  src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.cc
  src/core/lib/security/credentials/tls/grpc_tls_credentials_options.cc
  src/core/lib/security/credentials/tls/tls_credentials.cc
  src/core/lib/security/credentials/tls/tls_utils.cc
  src/core/lib/security/credentials/xds/xds_credentials.cc
  src/core/lib/security/security_connector/alts/alts_security_connector.cc
  src/core/lib/security/security_connector/fake/fake_security_connector.cc
  src/core/lib/security/security_connector/insecure/insecure_security_connector.cc
  src/core/lib/security/security_connector/load_system_roots_fallback.cc
  src/core/lib/security/security_connector/load_system_roots_linux.cc
  src/core/lib/security/security_connector/local/local_security_connector.cc
  src/core/lib/security/security_connector/security_connector.cc
  src/core/lib/security/security_connector/ssl/ssl_security_connector.cc
  src/core/lib/security/security_connector/ssl_utils.cc
  src/core/lib/security/security_connector/ssl_utils_config.cc
  src/core/lib/security/security_connector/tls/tls_security_connector.cc
  src/core/lib/security/transport/client_auth_filter.cc
  src/core/lib/security/transport/secure_endpoint.cc
  src/core/lib/security/transport/security_handshaker.cc
  src/core/lib/security/transport/server_auth_filter.cc
  src/core/lib/security/transport/tsi_error.cc
  src/core/lib/security/util/json_util.cc
  src/core/lib/slice/b64.cc
  src/core/lib/slice/percent_encoding.cc
  src/core/lib/slice/slice.cc
  src/core/lib/slice/slice_buffer.cc
  src/core/lib/slice/slice_intern.cc
  src/core/lib/slice/slice_string_helpers.cc
  src/core/lib/surface/api_trace.cc
  src/core/lib/surface/byte_buffer.cc
  src/core/lib/surface/byte_buffer_reader.cc
  src/core/lib/surface/call.cc
  src/core/lib/surface/call_details.cc
  src/core/lib/surface/call_log_batch.cc
  src/core/lib/surface/channel.cc
  src/core/lib/surface/channel_init.cc
  src/core/lib/surface/channel_ping.cc
  src/core/lib/surface/channel_stack_type.cc
  src/core/lib/surface/completion_queue.cc
  src/core/lib/surface/completion_queue_factory.cc
  src/core/lib/surface/event_string.cc
  src/core/lib/surface/init.cc
  src/core/lib/surface/init_secure.cc
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/authority_override.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/uri/uri_parser.cc
  src/core/plugin_registry/grpc_plugin_registry.cc
  src/core/tsi/alts/crypt/aes_gcm.cc
  src/core/tsi/alts/crypt/gsec.cc
  src/core/tsi/alts/frame_protector/alts_counter.cc
  src/core/tsi/alts/frame_protector/alts_crypter.cc
  src/core/tsi/alts/frame_protector/alts_frame_protector.cc
  src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc
  src/core/tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc
  src/core/tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc
  src/core/tsi/alts/frame_protector/frame_handler.cc
  src/core/tsi/alts/handshaker/alts_handshaker_client.cc
  src/core/tsi/alts/handshaker/alts_shared_resource.cc
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc
  src/core/tsi/alts/handshaker/alts_tsi_utils.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc
  src/core/tsi/fake_transport_security.cc
  src/core/tsi/local_transport_security.cc
  src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc
  src/core/tsi/ssl/session_cache/ssl_session_cache.cc
  src/core/tsi/ssl/session_cache/ssl_session_openssl.cc
  src/core/tsi/ssl_transport_security.cc
  src/core/tsi/transport_security.cc
  src/core/tsi/transport_security_grpc.cc
)

set_target_properties(grpc PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc PROPERTIES COMPILE_PDB_NAME "grpc"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  ${_gRPC_CARES_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
  ${_gRPC_RE2_LIBRARIES}
  ${_gRPC_UPB_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flat_hash_map
  absl::inlined_vector
  absl::bind_front
  absl::statusor
  gpr
  ${_gRPC_SSL_LIBRARIES}
  address_sorting
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc "-framework CoreFoundation")
endif()

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/census.h
  include/grpc/compression.h
  include/grpc/event_engine/channel_args.h
  include/grpc/event_engine/event_engine.h
  include/grpc/event_engine/port.h
  include/grpc/event_engine/slice_allocator.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_CSHARP_EXT)

add_library(grpc_csharp_ext SHARED
  src/csharp/ext/grpc_csharp_ext.c
)

set_target_properties(grpc_csharp_ext PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_csharp_ext PROPERTIES COMPILE_PDB_NAME "grpc_csharp_ext"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_csharp_ext.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_csharp_ext
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_csharp_ext
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
)


endif()
if(gRPC_BUILD_TESTS)

add_library(grpc_test_util
  test/core/util/cmdline.cc
  test/core/util/fuzzer_util.cc
  test/core/util/grpc_profiler.cc
  test/core/util/histogram.cc
  test/core/util/memory_counters.cc
  test/core/util/mock_endpoint.cc
  test/core/util/parse_hexstring.cc
  test/core/util/passthru_endpoint.cc
  test/core/util/port.cc
  test/core/util/port_isolated_runtime_environment.cc
  test/core/util/port_server_client.cc
  test/core/util/reconnect_server.cc
  test/core/util/resolve_localhost_ip46.cc
  test/core/util/slice_splitter.cc
  test/core/util/stack_tracer.cc
  test/core/util/subprocess_posix.cc
  test/core/util/subprocess_windows.cc
  test/core/util/test_config.cc
  test/core/util/test_tcp_server.cc
  test/core/util/tls_utils.cc
  test/core/util/tracer_util.cc
  test/core/util/trickle_endpoint.cc
)

set_target_properties(grpc_test_util PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util PROPERTIES COMPILE_PDB_NAME "grpc_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_test_util
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::failure_signal_handler
  absl::stacktrace
  absl::symbolize
  grpc
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_test_util "-framework CoreFoundation")
endif()


endif()
if(gRPC_BUILD_TESTS)

add_library(grpc_test_util_unsecure
  test/core/util/cmdline.cc
  test/core/util/fuzzer_util.cc
  test/core/util/grpc_profiler.cc
  test/core/util/histogram.cc
  test/core/util/memory_counters.cc
  test/core/util/mock_endpoint.cc
  test/core/util/parse_hexstring.cc
  test/core/util/passthru_endpoint.cc
  test/core/util/port.cc
  test/core/util/port_isolated_runtime_environment.cc
  test/core/util/port_server_client.cc
  test/core/util/reconnect_server.cc
  test/core/util/resolve_localhost_ip46.cc
  test/core/util/slice_splitter.cc
  test/core/util/stack_tracer.cc
  test/core/util/subprocess_posix.cc
  test/core/util/subprocess_windows.cc
  test/core/util/test_config.cc
  test/core/util/test_tcp_server.cc
  test/core/util/tracer_util.cc
  test/core/util/trickle_endpoint.cc
)

set_target_properties(grpc_test_util_unsecure PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_test_util_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_test_util_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_test_util_unsecure
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::failure_signal_handler
  absl::stacktrace
  absl::symbolize
  grpc_unsecure
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_test_util_unsecure "-framework CoreFoundation")
endif()


endif()

add_library(grpc_unsecure
  src/core/ext/filters/census/grpc_context.cc
  src/core/ext/filters/client_channel/backend_metric.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/config_selector.cc
  src/core/ext/filters/client_channel/dynamic_filters.cc
  src/core/ext/filters/client_channel/global_subchannel_pool.cc
  src/core/ext/filters/client_channel/health/health_check_client.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy/address_filtering.cc
  src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc
  src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc
  src/core/ext/filters/client_channel/lb_policy/priority/priority.cc
  src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc
  src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/local_subchannel_pool.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_libuv.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_libuv.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.cc
  src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/resolver_result_parsing.cc
  src/core/ext/filters/client_channel/retry_filter.cc
  src/core/ext/filters/client_channel/retry_service_config.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/server_address.cc
  src/core/ext/filters/client_channel/service_config.cc
  src/core/ext/filters/client_channel/service_config_channel_arg_filter.cc
  src/core/ext/filters/client_channel/service_config_parser.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_pool_interface.cc
  src/core/ext/filters/client_idle/client_idle_filter.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_filter.cc
  src/core/ext/filters/fault_injection/service_config_parser.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/message_compress/message_decompress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/max_age/max_age_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc
  src/core/ext/filters/workarounds/workaround_utils.cc
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  src/core/ext/transport/chttp2/transport/context_list.cc
  src/core/ext/transport/chttp2/transport/flow_control.cc
  src/core/ext/transport/chttp2/transport/frame_data.cc
  src/core/ext/transport/chttp2/transport/frame_goaway.cc
  src/core/ext/transport/chttp2/transport/frame_ping.cc
  src/core/ext/transport/chttp2/transport/frame_rst_stream.cc
  src/core/ext/transport/chttp2/transport/frame_settings.cc
  src/core/ext/transport/chttp2/transport/frame_window_update.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder.cc
  src/core/ext/transport/chttp2/transport/hpack_parser.cc
  src/core/ext/transport/chttp2/transport/hpack_table.cc
  src/core/ext/transport/chttp2/transport/http2_settings.cc
  src/core/ext/transport/chttp2/transport/huffsyms.cc
  src/core/ext/transport/chttp2/transport/incoming_metadata.cc
  src/core/ext/transport/chttp2/transport/parsing.cc
  src/core/ext/transport/chttp2/transport/stream_lists.cc
  src/core/ext/transport/chttp2/transport/stream_map.cc
  src/core/ext/transport/chttp2/transport/varint.cc
  src/core/ext/transport/chttp2/transport/writing.cc
  src/core/ext/transport/inproc/inproc_plugin.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.c
  src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.c
  src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.c
  src/core/ext/upb-generated/validate/validate.upb.c
  src/core/lib/address_utils/parse_address.cc
  src/core/lib/address_utils/sockaddr_utils.cc
  src/core/lib/avl/avl.cc
  src/core/lib/backoff/backoff.cc
  src/core/lib/channel/channel_args.cc
  src/core/lib/channel/channel_stack.cc
  src/core/lib/channel/channel_stack_builder.cc
  src/core/lib/channel/channel_trace.cc
  src/core/lib/channel/channelz.cc
  src/core/lib/channel/channelz_registry.cc
  src/core/lib/channel/connected_channel.cc
  src/core/lib/channel/handshaker.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_args.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/debug/trace.cc
  src/core/lib/event_engine/slice_allocator.cc
  src/core/lib/event_engine/sockaddr.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/buffer_list.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/cfstream_handle.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/dualstack_socket_posix.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_cfstream.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/error_cfstream.cc
  src/core/lib/iomgr/ev_apple.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/executor/mpmcqueue.cc
  src/core/lib/iomgr/executor/threadpool.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/grpc_if_nametoindex_posix.cc
  src/core/lib/iomgr/grpc_if_nametoindex_unsupported.cc
  src/core/lib/iomgr/internal_errqueue.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_posix_cfstream.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/polling_entity.cc
  src/core/lib/iomgr/pollset.cc
  src/core/lib/iomgr/pollset_custom.cc
  src/core/lib/iomgr/pollset_set.cc
  src/core/lib/iomgr/pollset_set_custom.cc
  src/core/lib/iomgr/pollset_set_windows.cc
  src/core/lib/iomgr/pollset_uv.cc
  src/core/lib/iomgr/pollset_windows.cc
  src/core/lib/iomgr/resolve_address.cc
  src/core/lib/iomgr/resolve_address_custom.cc
  src/core/lib/iomgr/resolve_address_posix.cc
  src/core/lib/iomgr/resolve_address_windows.cc
  src/core/lib/iomgr/resource_quota.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
  src/core/lib/iomgr/tcp_client_cfstream.cc
  src/core/lib/iomgr/tcp_client_custom.cc
  src/core/lib/iomgr/tcp_client_posix.cc
  src/core/lib/iomgr/tcp_client_windows.cc
  src/core/lib/iomgr/tcp_custom.cc
  src/core/lib/iomgr/tcp_posix.cc
  src/core/lib/iomgr/tcp_server.cc
  src/core/lib/iomgr/tcp_server_custom.cc
  src/core/lib/iomgr/tcp_server_posix.cc
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc
  src/core/lib/iomgr/tcp_server_windows.cc
  src/core/lib/iomgr/tcp_uv.cc
  src/core/lib/iomgr/tcp_windows.cc
  src/core/lib/iomgr/time_averaged_stats.cc
  src/core/lib/iomgr/timer.cc
  src/core/lib/iomgr/timer_custom.cc
  src/core/lib/iomgr/timer_generic.cc
  src/core/lib/iomgr/timer_heap.cc
  src/core/lib/iomgr/timer_manager.cc
  src/core/lib/iomgr/timer_uv.cc
  src/core/lib/iomgr/udp_server.cc
  src/core/lib/iomgr/unix_sockets_posix.cc
  src/core/lib/iomgr/unix_sockets_posix_noop.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/iomgr/work_serializer.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_util.cc
  src/core/lib/json/json_writer.cc
  src/core/lib/slice/b64.cc
  src/core/lib/slice/percent_encoding.cc
  src/core/lib/slice/slice.cc
  src/core/lib/slice/slice_buffer.cc
  src/core/lib/slice/slice_intern.cc
  src/core/lib/slice/slice_string_helpers.cc
  src/core/lib/surface/api_trace.cc
  src/core/lib/surface/byte_buffer.cc
  src/core/lib/surface/byte_buffer_reader.cc
  src/core/lib/surface/call.cc
  src/core/lib/surface/call_details.cc
  src/core/lib/surface/call_log_batch.cc
  src/core/lib/surface/channel.cc
  src/core/lib/surface/channel_init.cc
  src/core/lib/surface/channel_ping.cc
  src/core/lib/surface/channel_stack_type.cc
  src/core/lib/surface/completion_queue.cc
  src/core/lib/surface/completion_queue_factory.cc
  src/core/lib/surface/event_string.cc
  src/core/lib/surface/init.cc
  src/core/lib/surface/init_unsecure.cc
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/authority_override.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/uri/uri_parser.cc
  src/core/plugin_registry/grpc_unsecure_plugin_registry.cc
)

set_target_properties(grpc_unsecure PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  ${_gRPC_CARES_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
  ${_gRPC_RE2_LIBRARIES}
  ${_gRPC_UPB_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flat_hash_map
  absl::inlined_vector
  absl::statusor
  gpr
  address_sorting
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_unsecure "-framework CoreFoundation")
endif()

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/census.h
  include/grpc/compression.h
  include/grpc/event_engine/channel_args.h
  include/grpc/event_engine/event_engine.h
  include/grpc/event_engine/port.h
  include/grpc/event_engine/slice_allocator.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_TESTS)

if(gRPC_BUILD_CODEGEN)
add_library(benchmark_helpers
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_mock.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/microbenchmarks/helpers.cc
)

set_target_properties(benchmark_helpers PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(benchmark_helpers PROPERTIES COMPILE_PDB_NAME "benchmark_helpers"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/benchmark_helpers.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(benchmark_helpers
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(benchmark_helpers
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_unsecure
  grpc_test_util_unsecure
  grpc++_test_config
)

endif()

endif()

add_library(grpc++
  src/cpp/client/channel_cc.cc
  src/cpp/client/client_callback.cc
  src/cpp/client/client_context.cc
  src/cpp/client/client_interceptor.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/credentials_cc.cc
  src/cpp/client/insecure_credentials.cc
  src/cpp/client/secure_credentials.cc
  src/cpp/client/xds_credentials.cc
  src/cpp/codegen/codegen_init.cc
  src/cpp/common/alarm.cc
  src/cpp/common/auth_property_iterator.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/secure_auth_context.cc
  src/cpp/common/secure_channel_arguments.cc
  src/cpp/common/secure_create_auth_context.cc
  src/cpp/common/tls_certificate_provider.cc
  src/cpp/common/tls_credentials_options.cc
  src/cpp/common/tls_credentials_options_util.cc
  src/cpp/common/validate_service_config.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/external_connection_acceptor_impl.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/server/secure_server_credentials.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_callback.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/server/xds_server_credentials.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
)

set_target_properties(grpc++ PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++ PROPERTIES COMPILE_PDB_NAME "grpc++"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
)

foreach(_hdr
  include/grpc++/alarm.h
  include/grpc++/channel.h
  include/grpc++/client_context.h
  include/grpc++/completion_queue.h
  include/grpc++/create_channel.h
  include/grpc++/create_channel_posix.h
  include/grpc++/ext/health_check_service_server_builder_option.h
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/health_check_service_interface.h
  include/grpc++/impl/call.h
  include/grpc++/impl/channel_argument_option.h
  include/grpc++/impl/client_unary_call.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
  include/grpc++/impl/codegen/byte_buffer.h
  include/grpc++/impl/codegen/call.h
  include/grpc++/impl/codegen/call_hook.h
  include/grpc++/impl/codegen/channel_interface.h
  include/grpc++/impl/codegen/client_context.h
  include/grpc++/impl/codegen/client_unary_call.h
  include/grpc++/impl/codegen/completion_queue.h
  include/grpc++/impl/codegen/completion_queue_tag.h
  include/grpc++/impl/codegen/config.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc++/impl/codegen/core_codegen.h
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/slice.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc++/impl/grpc_library.h
  include/grpc++/impl/method_handler_impl.h
  include/grpc++/impl/rpc_method.h
  include/grpc++/impl/rpc_service_method.h
  include/grpc++/impl/serialization_traits.h
  include/grpc++/impl/server_builder_option.h
  include/grpc++/impl/server_builder_plugin.h
  include/grpc++/impl/server_initializer.h
  include/grpc++/impl/service_type.h
  include/grpc++/resource_quota.h
  include/grpc++/security/auth_context.h
  include/grpc++/security/auth_metadata_processor.h
  include/grpc++/security/credentials.h
  include/grpc++/security/server_credentials.h
  include/grpc++/server.h
  include/grpc++/server_builder.h
  include/grpc++/server_context.h
  include/grpc++/server_posix.h
  include/grpc++/support/async_stream.h
  include/grpc++/support/async_unary_call.h
  include/grpc++/support/byte_buffer.h
  include/grpc++/support/channel_arguments.h
  include/grpc++/support/config.h
  include/grpc++/support/slice.h
  include/grpc++/support/status.h
  include/grpc++/support/status_code_enum.h
  include/grpc++/support/string_ref.h
  include/grpc++/support/stub_options.h
  include/grpc++/support/sync_stream.h
  include/grpc++/support/time.h
  include/grpcpp/alarm.h
  include/grpcpp/channel.h
  include/grpcpp/client_context.h
  include/grpcpp/completion_queue.h
  include/grpcpp/create_channel.h
  include/grpcpp/create_channel_posix.h
  include/grpcpp/ext/health_check_service_server_builder_option.h
  include/grpcpp/generic/async_generic_service.h
  include/grpcpp/generic/generic_stub.h
  include/grpcpp/grpcpp.h
  include/grpcpp/health_check_service_interface.h
  include/grpcpp/impl/call.h
  include/grpcpp/impl/channel_argument_option.h
  include/grpcpp/impl/client_unary_call.h
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/call_op_set.h
  include/grpcpp/impl/codegen/call_op_set_interface.h
  include/grpcpp/impl/codegen/callback_common.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_callback.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_interceptor.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/core_codegen.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/delegating_channel.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/intercepted_channel.h
  include/grpcpp/impl/codegen/interceptor.h
  include/grpcpp/impl/codegen/interceptor_common.h
  include/grpcpp/impl/codegen/message_allocator.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_callback.h
  include/grpcpp/impl/codegen/server_callback_handlers.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interceptor.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpcpp/impl/grpc_library.h
  include/grpcpp/impl/method_handler_impl.h
  include/grpcpp/impl/rpc_method.h
  include/grpcpp/impl/rpc_service_method.h
  include/grpcpp/impl/serialization_traits.h
  include/grpcpp/impl/server_builder_option.h
  include/grpcpp/impl/server_builder_plugin.h
  include/grpcpp/impl/server_initializer.h
  include/grpcpp/impl/service_type.h
  include/grpcpp/resource_quota.h
  include/grpcpp/security/auth_context.h
  include/grpcpp/security/auth_metadata_processor.h
  include/grpcpp/security/credentials.h
  include/grpcpp/security/server_credentials.h
  include/grpcpp/security/tls_certificate_provider.h
  include/grpcpp/security/tls_credentials_options.h
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/client_callback.h
  include/grpcpp/support/client_interceptor.h
  include/grpcpp/support/config.h
  include/grpcpp/support/interceptor.h
  include/grpcpp/support/message_allocator.h
  include/grpcpp/support/method_handler.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/server_callback.h
  include/grpcpp/support/server_interceptor.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
  include/grpcpp/support/validate_service_config.h
  include/grpcpp/xds_server_builder.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++ EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc++_alts
  src/cpp/common/alts_context.cc
  src/cpp/common/alts_util.cc
)

set_target_properties(grpc++_alts PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_alts PROPERTIES COMPILE_PDB_NAME "grpc++_alts"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_alts.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_alts
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_alts
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpcpp/security/alts_context.h
  include/grpcpp/security/alts_util.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_alts EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc++_error_details
  src/cpp/util/error_details.cc
)

set_target_properties(grpc++_error_details PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_error_details PROPERTIES COMPILE_PDB_NAME "grpc++_error_details"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_error_details.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_error_details
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_error_details
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpc++/support/error_details.h
  include/grpcpp/support/error_details.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_error_details EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


if(gRPC_BUILD_CODEGEN)
add_library(grpc++_reflection
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
  src/cpp/ext/proto_server_reflection.cc
  src/cpp/ext/proto_server_reflection_plugin.cc
)

set_target_properties(grpc++_reflection PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_reflection PROPERTIES COMPILE_PDB_NAME "grpc++_reflection"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_reflection.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_reflection
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_reflection
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpc++/ext/proto_server_reflection_plugin.h
  include/grpcpp/ext/proto_server_reflection_plugin.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif()

if(gRPC_BUILD_CODEGEN)

if(gRPC_INSTALL)
  install(TARGETS grpc++_reflection EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test
  src/cpp/client/channel_test_peer.cc
)

set_target_properties(grpc++_test PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test PROPERTIES COMPILE_PDB_NAME "grpc++_test"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpc++/test/mock_stream.h
  include/grpc++/test/server_context_test_spouse.h
  include/grpcpp/test/channel_test_peer.h
  include/grpcpp/test/default_reactor_test_peer.h
  include/grpcpp/test/mock_stream.h
  include/grpcpp/test/server_context_test_spouse.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test_config
  test/cpp/util/test_config_cc.cc
)

set_target_properties(grpc++_test_config PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_config PROPERTIES COMPILE_PDB_NAME "grpc++_test_config"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_config.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test_config
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test_config
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags_parse
  gpr
)


endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test_util
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/cpp/util/byte_buffer_proto_helper.cc
  test/cpp/util/create_test_channel.cc
  test/cpp/util/string_ref_helper.cc
  test/cpp/util/subprocess.cc
  test/cpp/util/test_credentials_provider.cc
)

set_target_properties(grpc++_test_util PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_util PROPERTIES COMPILE_PDB_NAME "grpc++_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test_util
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc++
  grpc_test_util
)


endif()

add_library(grpc++_unsecure
  src/cpp/client/channel_cc.cc
  src/cpp/client/client_callback.cc
  src/cpp/client/client_context.cc
  src/cpp/client/client_interceptor.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/credentials_cc.cc
  src/cpp/client/insecure_credentials.cc
  src/cpp/codegen/codegen_init.cc
  src/cpp/common/alarm.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/insecure_create_auth_context.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/validate_service_config.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/external_connection_acceptor_impl.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_callback.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
)

set_target_properties(grpc++_unsecure PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_unsecure PROPERTIES COMPILE_PDB_NAME "grpc++_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_unsecure
)

foreach(_hdr
  include/grpc++/alarm.h
  include/grpc++/channel.h
  include/grpc++/client_context.h
  include/grpc++/completion_queue.h
  include/grpc++/create_channel.h
  include/grpc++/create_channel_posix.h
  include/grpc++/ext/health_check_service_server_builder_option.h
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/health_check_service_interface.h
  include/grpc++/impl/call.h
  include/grpc++/impl/channel_argument_option.h
  include/grpc++/impl/client_unary_call.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
  include/grpc++/impl/codegen/byte_buffer.h
  include/grpc++/impl/codegen/call.h
  include/grpc++/impl/codegen/call_hook.h
  include/grpc++/impl/codegen/channel_interface.h
  include/grpc++/impl/codegen/client_context.h
  include/grpc++/impl/codegen/client_unary_call.h
  include/grpc++/impl/codegen/completion_queue.h
  include/grpc++/impl/codegen/completion_queue_tag.h
  include/grpc++/impl/codegen/config.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc++/impl/codegen/core_codegen.h
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/slice.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc++/impl/grpc_library.h
  include/grpc++/impl/method_handler_impl.h
  include/grpc++/impl/rpc_method.h
  include/grpc++/impl/rpc_service_method.h
  include/grpc++/impl/serialization_traits.h
  include/grpc++/impl/server_builder_option.h
  include/grpc++/impl/server_builder_plugin.h
  include/grpc++/impl/server_initializer.h
  include/grpc++/impl/service_type.h
  include/grpc++/resource_quota.h
  include/grpc++/security/auth_context.h
  include/grpc++/security/auth_metadata_processor.h
  include/grpc++/security/credentials.h
  include/grpc++/security/server_credentials.h
  include/grpc++/server.h
  include/grpc++/server_builder.h
  include/grpc++/server_context.h
  include/grpc++/server_posix.h
  include/grpc++/support/async_stream.h
  include/grpc++/support/async_unary_call.h
  include/grpc++/support/byte_buffer.h
  include/grpc++/support/channel_arguments.h
  include/grpc++/support/config.h
  include/grpc++/support/slice.h
  include/grpc++/support/status.h
  include/grpc++/support/status_code_enum.h
  include/grpc++/support/string_ref.h
  include/grpc++/support/stub_options.h
  include/grpc++/support/sync_stream.h
  include/grpc++/support/time.h
  include/grpcpp/alarm.h
  include/grpcpp/channel.h
  include/grpcpp/client_context.h
  include/grpcpp/completion_queue.h
  include/grpcpp/create_channel.h
  include/grpcpp/create_channel_posix.h
  include/grpcpp/ext/health_check_service_server_builder_option.h
  include/grpcpp/generic/async_generic_service.h
  include/grpcpp/generic/generic_stub.h
  include/grpcpp/grpcpp.h
  include/grpcpp/health_check_service_interface.h
  include/grpcpp/impl/call.h
  include/grpcpp/impl/channel_argument_option.h
  include/grpcpp/impl/client_unary_call.h
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/call_op_set.h
  include/grpcpp/impl/codegen/call_op_set_interface.h
  include/grpcpp/impl/codegen/callback_common.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_callback.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_interceptor.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/core_codegen.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/delegating_channel.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/intercepted_channel.h
  include/grpcpp/impl/codegen/interceptor.h
  include/grpcpp/impl/codegen/interceptor_common.h
  include/grpcpp/impl/codegen/message_allocator.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_callback.h
  include/grpcpp/impl/codegen/server_callback_handlers.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interceptor.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpcpp/impl/grpc_library.h
  include/grpcpp/impl/method_handler_impl.h
  include/grpcpp/impl/rpc_method.h
  include/grpcpp/impl/rpc_service_method.h
  include/grpcpp/impl/serialization_traits.h
  include/grpcpp/impl/server_builder_option.h
  include/grpcpp/impl/server_builder_plugin.h
  include/grpcpp/impl/server_initializer.h
  include/grpcpp/impl/service_type.h
  include/grpcpp/resource_quota.h
  include/grpcpp/security/auth_context.h
  include/grpcpp/security/auth_metadata_processor.h
  include/grpcpp/security/credentials.h
  include/grpcpp/security/server_credentials.h
  include/grpcpp/security/tls_certificate_provider.h
  include/grpcpp/security/tls_credentials_options.h
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/client_callback.h
  include/grpcpp/support/client_interceptor.h
  include/grpcpp/support/config.h
  include/grpcpp/support/interceptor.h
  include/grpcpp/support/message_allocator.h
  include/grpcpp/support/method_handler.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/server_callback.h
  include/grpcpp/support/server_interceptor.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
  include/grpcpp/support/validate_service_config.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc_plugin_support
  src/compiler/cpp_generator.cc
  src/compiler/csharp_generator.cc
  src/compiler/node_generator.cc
  src/compiler/objective_c_generator.cc
  src/compiler/php_generator.cc
  src/compiler/python_generator.cc
  src/compiler/ruby_generator.cc
)

set_target_properties(grpc_plugin_support PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_plugin_support PROPERTIES COMPILE_PDB_NAME "grpc_plugin_support"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_plugin_support.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_plugin_support
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc_plugin_support
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)

foreach(_hdr
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc_plugin_support EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


# grpcpp_channelz doesn't build with protobuf-lite
# See https://github.com/grpc/grpc/issues/19473
if(gRPC_BUILD_CODEGEN AND NOT gRPC_USE_PROTO_LITE)
add_library(grpcpp_channelz
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  src/cpp/server/channelz/channelz_service.cc
  src/cpp/server/channelz/channelz_service_plugin.cc
)

set_target_properties(grpcpp_channelz PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpcpp_channelz PROPERTIES COMPILE_PDB_NAME "grpcpp_channelz"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpcpp_channelz.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpcpp_channelz
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpcpp_channelz
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpcpp/ext/channelz_service_plugin.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif()

if(gRPC_BUILD_CODEGEN)

# grpcpp_channelz doesn't build with protobuf-lite, so no install required
# See https://github.com/grpc/grpc/issues/22826
if(gRPC_INSTALL AND NOT gRPC_USE_PROTO_LITE)
  install(TARGETS grpcpp_channelz EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()

add_library(upb
  third_party/upb/upb/decode_fast.c
  third_party/upb/upb/decode.c
  third_party/upb/upb/def.c
  third_party/upb/upb/encode.c
  third_party/upb/upb/msg.c
  third_party/upb/upb/reflection.c
  third_party/upb/upb/table.c
  third_party/upb/upb/text_encode.c
  third_party/upb/upb/upb.c
  src/core/ext/upb-generated/google/protobuf/descriptor.upb.c
  src/core/ext/upbdefs-generated/google/protobuf/descriptor.upbdefs.c
)

set_target_properties(upb PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb PROPERTIES COMPILE_PDB_NAME "upb"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS upb EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()



add_executable(check_epollexclusive
  test/build/check_epollexclusive.c
)

target_include_directories(check_epollexclusive
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(check_epollexclusive
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)



add_executable(gen_hpack_tables
  tools/codegen/core/gen_hpack_tables.cc
)

target_include_directories(gen_hpack_tables
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(gen_hpack_tables
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)



add_executable(gen_legal_metadata_characters
  tools/codegen/core/gen_legal_metadata_characters.cc
)

target_include_directories(gen_legal_metadata_characters
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(gen_legal_metadata_characters
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



add_executable(gen_percent_encoding_tables
  tools/codegen/core/gen_percent_encoding_tables.cc
)

target_include_directories(gen_percent_encoding_tables
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(gen_percent_encoding_tables
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)


if(gRPC_BUILD_TESTS)

add_executable(algorithm_test
  test/core/compression/algorithm_test.cc
)

target_include_directories(algorithm_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(algorithm_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alloc_test
  test/core/gpr/alloc_test.cc
)

target_include_directories(alloc_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alloc_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alpn_test
  test/core/transport/chttp2/alpn_test.cc
)

target_include_directories(alpn_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alpn_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_counter_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/frame_protector/alts_counter_test.cc
)

target_include_directories(alts_counter_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_counter_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_crypt_test
  test/core/tsi/alts/crypt/aes_gcm_test.cc
  test/core/tsi/alts/crypt/gsec_test_util.cc
)

target_include_directories(alts_crypt_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_crypt_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_crypter_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/frame_protector/alts_crypter_test.cc
)

target_include_directories(alts_crypter_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_crypter_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_frame_protector_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/frame_protector/alts_frame_protector_test.cc
  test/core/tsi/transport_security_test_lib.cc
)

target_include_directories(alts_frame_protector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_frame_protector_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_grpc_record_protocol_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_test.cc
)

target_include_directories(alts_grpc_record_protocol_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_grpc_record_protocol_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_handshaker_client_test
  test/core/tsi/alts/handshaker/alts_handshaker_client_test.cc
  test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.cc
)

target_include_directories(alts_handshaker_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_handshaker_client_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_iovec_record_protocol_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol_test.cc
)

target_include_directories(alts_iovec_record_protocol_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_iovec_record_protocol_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_security_connector_test
  test/core/security/alts_security_connector_test.cc
)

target_include_directories(alts_security_connector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_security_connector_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_tsi_handshaker_test
  test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.cc
  test/core/tsi/alts/handshaker/alts_tsi_handshaker_test.cc
)

target_include_directories(alts_tsi_handshaker_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_tsi_handshaker_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_tsi_utils_test
  test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.cc
  test/core/tsi/alts/handshaker/alts_tsi_utils_test.cc
)

target_include_directories(alts_tsi_utils_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_tsi_utils_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_zero_copy_grpc_protector_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector_test.cc
)

target_include_directories(alts_zero_copy_grpc_protector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(alts_zero_copy_grpc_protector_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(arena_test
  test/core/gpr/arena_test.cc
)

target_include_directories(arena_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(arena_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(auth_context_test
  test/core/security/auth_context_test.cc
)

target_include_directories(auth_context_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(auth_context_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(avl_test
  test/core/avl/avl_test.cc
)

target_include_directories(avl_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(avl_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(b64_test
  test/core/slice/b64_test.cc
)

target_include_directories(b64_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(b64_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(bad_server_response_test
  test/core/end2end/bad_server_response_test.cc
  test/core/end2end/cq_verifier.cc
)

target_include_directories(bad_server_response_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(bad_server_response_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(bad_ssl_alpn_test
    test/core/bad_ssl/bad_ssl_test.cc
    test/core/end2end/cq_verifier.cc
  )

  target_include_directories(bad_ssl_alpn_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(bad_ssl_alpn_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(bad_ssl_cert_test
    test/core/bad_ssl/bad_ssl_test.cc
    test/core/end2end/cq_verifier.cc
  )

  target_include_directories(bad_ssl_cert_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(bad_ssl_cert_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(bin_decoder_test
  test/core/transport/chttp2/bin_decoder_test.cc
)

target_include_directories(bin_decoder_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(bin_decoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(bin_encoder_test
  test/core/transport/chttp2/bin_encoder_test.cc
)

target_include_directories(bin_encoder_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(bin_encoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(buffer_list_test
  test/core/iomgr/buffer_list_test.cc
)

target_include_directories(buffer_list_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(buffer_list_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_args_test
  test/core/channel/channel_args_test.cc
)

target_include_directories(channel_args_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(channel_args_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_create_test
  test/core/surface/channel_create_test.cc
)

target_include_directories(channel_create_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(channel_create_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_stack_builder_test
  test/core/channel/channel_stack_builder_test.cc
)

target_include_directories(channel_stack_builder_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(channel_stack_builder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_stack_test
  test/core/channel/channel_stack_test.cc
)

target_include_directories(channel_stack_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(channel_stack_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(check_gcp_environment_linux_test
  test/core/security/check_gcp_environment_linux_test.cc
)

target_include_directories(check_gcp_environment_linux_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(check_gcp_environment_linux_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(check_gcp_environment_windows_test
  test/core/security/check_gcp_environment_windows_test.cc
)

target_include_directories(check_gcp_environment_windows_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(check_gcp_environment_windows_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(client_ssl_test
    test/core/handshake/client_ssl.cc
  )

  target_include_directories(client_ssl_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(client_ssl_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(cmdline_test
  test/core/util/cmdline_test.cc
)

target_include_directories(cmdline_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(cmdline_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(combiner_test
    test/core/iomgr/combiner_test.cc
  )

  target_include_directories(combiner_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(combiner_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(completion_queue_threading_test
  test/core/surface/completion_queue_threading_test.cc
)

target_include_directories(completion_queue_threading_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(completion_queue_threading_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(compression_test
  test/core/compression/compression_test.cc
)

target_include_directories(compression_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(compression_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(concurrent_connectivity_test
  test/core/surface/concurrent_connectivity_test.cc
)

target_include_directories(concurrent_connectivity_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(concurrent_connectivity_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(connection_refused_test
  test/core/end2end/connection_refused_test.cc
  test/core/end2end/cq_verifier.cc
)

target_include_directories(connection_refused_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(connection_refused_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(cpu_test
  test/core/gpr/cpu_test.cc
)

target_include_directories(cpu_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(cpu_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(dns_resolver_connectivity_using_ares_test
  test/core/client_channel/resolvers/dns_resolver_connectivity_test.cc
)

target_include_directories(dns_resolver_connectivity_using_ares_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_connectivity_using_ares_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(dns_resolver_connectivity_using_native_test
  test/core/client_channel/resolvers/dns_resolver_connectivity_test.cc
)

target_include_directories(dns_resolver_connectivity_using_native_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_connectivity_using_native_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(dns_resolver_cooldown_test
  test/core/client_channel/resolvers/dns_resolver_cooldown_test.cc
)

target_include_directories(dns_resolver_cooldown_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_cooldown_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(dns_resolver_test
  test/core/client_channel/resolvers/dns_resolver_test.cc
)

target_include_directories(dns_resolver_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(dualstack_socket_test
    test/core/end2end/cq_verifier.cc
    test/core/end2end/dualstack_socket_test.cc
  )

  target_include_directories(dualstack_socket_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(dualstack_socket_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(endpoint_pair_test
  test/core/iomgr/endpoint_pair_test.cc
  test/core/iomgr/endpoint_tests.cc
)

target_include_directories(endpoint_pair_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(endpoint_pair_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(env_test
  test/core/gpr/env_test.cc
)

target_include_directories(env_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(env_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(error_test
  test/core/iomgr/endpoint_tests.cc
  test/core/iomgr/error_test.cc
)

target_include_directories(error_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(error_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(ev_epollex_linux_test
    test/core/iomgr/ev_epollex_linux_test.cc
  )

  target_include_directories(ev_epollex_linux_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(ev_epollex_linux_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(fake_resolver_test
  test/core/client_channel/resolvers/fake_resolver_test.cc
)

target_include_directories(fake_resolver_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(fake_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(fake_transport_security_test
  test/core/tsi/fake_transport_security_test.cc
  test/core/tsi/transport_security_test_lib.cc
)

target_include_directories(fake_transport_security_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(fake_transport_security_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(fd_conservation_posix_test
    test/core/iomgr/fd_conservation_posix_test.cc
  )

  target_include_directories(fd_conservation_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(fd_conservation_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(fd_posix_test
    test/core/iomgr/fd_posix_test.cc
  )

  target_include_directories(fd_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(fd_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(fling_stream_test
    test/core/end2end/data/client_certs.cc
    test/core/end2end/data/server1_cert.cc
    test/core/end2end/data/server1_key.cc
    test/core/end2end/data/test_root_cert.cc
    test/core/fling/fling_stream_test.cc
  )

  target_include_directories(fling_stream_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(fling_stream_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(fling_test
    test/core/end2end/data/client_certs.cc
    test/core/end2end/data/server1_cert.cc
    test/core/end2end/data/server1_key.cc
    test/core/end2end/data/test_root_cert.cc
    test/core/fling/fling_test.cc
  )

  target_include_directories(fling_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(fling_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(fork_test
    test/core/gprpp/fork_test.cc
  )

  target_include_directories(fork_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(fork_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(format_request_test
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/core/http/format_request_test.cc
)

target_include_directories(format_request_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(format_request_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(frame_handler_test
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/frame_protector/frame_handler_test.cc
)

target_include_directories(frame_handler_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(frame_handler_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(goaway_server_test
  test/core/end2end/cq_verifier.cc
  test/core/end2end/goaway_server_test.cc
)

target_include_directories(goaway_server_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(goaway_server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_alts_credentials_options_test
  test/core/security/grpc_alts_credentials_options_test.cc
)

target_include_directories(grpc_alts_credentials_options_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(grpc_alts_credentials_options_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_byte_buffer_reader_test
  test/core/surface/byte_buffer_reader_test.cc
)

target_include_directories(grpc_byte_buffer_reader_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(grpc_byte_buffer_reader_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_completion_queue_test
  test/core/surface/completion_queue_test.cc
)

target_include_directories(grpc_completion_queue_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(grpc_completion_queue_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_ipv6_loopback_available_test
  test/core/iomgr/grpc_ipv6_loopback_available_test.cc
)

target_include_directories(grpc_ipv6_loopback_available_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(grpc_ipv6_loopback_available_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(handshake_server_with_readahead_handshaker_test
    test/core/handshake/readahead_handshaker_server_ssl.cc
    test/core/handshake/server_ssl_common.cc
  )

  target_include_directories(handshake_server_with_readahead_handshaker_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(handshake_server_with_readahead_handshaker_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(handshake_verify_peer_options_test
    test/core/handshake/verify_peer_options.cc
  )

  target_include_directories(handshake_verify_peer_options_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(handshake_verify_peer_options_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(histogram_test
  test/core/util/histogram_test.cc
)

target_include_directories(histogram_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(histogram_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(host_port_test
  test/core/gprpp/host_port_test.cc
)

target_include_directories(host_port_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(host_port_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(hpack_encoder_test
  test/core/transport/chttp2/hpack_encoder_test.cc
)

target_include_directories(hpack_encoder_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(hpack_encoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(hpack_parser_test
  test/core/transport/chttp2/hpack_parser_test.cc
)

target_include_directories(hpack_parser_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(hpack_parser_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(hpack_table_test
  test/core/transport/chttp2/hpack_table_test.cc
)

target_include_directories(hpack_table_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(hpack_table_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(httpcli_test
    test/core/end2end/data/client_certs.cc
    test/core/end2end/data/server1_cert.cc
    test/core/end2end/data/server1_key.cc
    test/core/end2end/data/test_root_cert.cc
    test/core/http/httpcli_test.cc
  )

  target_include_directories(httpcli_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(httpcli_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(httpscli_test
    test/core/end2end/data/client_certs.cc
    test/core/end2end/data/server1_cert.cc
    test/core/end2end/data/server1_key.cc
    test/core/end2end/data/test_root_cert.cc
    test/core/http/httpscli_test.cc
  )

  target_include_directories(httpscli_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(httpscli_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(inproc_callback_test
  test/core/end2end/inproc_callback_test.cc
)

target_include_directories(inproc_callback_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(inproc_callback_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(invalid_call_argument_test
  test/core/end2end/cq_verifier.cc
  test/core/end2end/invalid_call_argument_test.cc
)

target_include_directories(invalid_call_argument_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(invalid_call_argument_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(json_token_test
  test/core/security/json_token_test.cc
)

target_include_directories(json_token_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(json_token_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(jwt_verifier_test
  test/core/security/jwt_verifier_test.cc
)

target_include_directories(jwt_verifier_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(jwt_verifier_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(lame_client_test
  test/core/end2end/cq_verifier.cc
  test/core/surface/lame_client_test.cc
)

target_include_directories(lame_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(lame_client_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(load_file_test
  test/core/iomgr/load_file_test.cc
)

target_include_directories(load_file_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(load_file_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(manual_constructor_test
  test/core/gprpp/manual_constructor_test.cc
)

target_include_directories(manual_constructor_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(manual_constructor_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(message_compress_test
  test/core/compression/message_compress_test.cc
)

target_include_directories(message_compress_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(message_compress_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(metadata_test
  test/core/transport/metadata_test.cc
)

target_include_directories(metadata_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(metadata_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(minimal_stack_is_minimal_test
  test/core/channel/minimal_stack_is_minimal_test.cc
)

target_include_directories(minimal_stack_is_minimal_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(minimal_stack_is_minimal_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(mpmcqueue_test
  test/core/iomgr/mpmcqueue_test.cc
)

target_include_directories(mpmcqueue_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(mpmcqueue_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(mpscq_test
    test/core/gprpp/mpscq_test.cc
  )

  target_include_directories(mpscq_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(mpscq_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(multiple_server_queues_test
  test/core/end2end/cq_verifier.cc
  test/core/end2end/multiple_server_queues_test.cc
)

target_include_directories(multiple_server_queues_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(multiple_server_queues_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(murmur_hash_test
  test/core/gpr/murmur_hash_test.cc
)

target_include_directories(murmur_hash_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(murmur_hash_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(no_server_test
  test/core/end2end/cq_verifier.cc
  test/core/end2end/no_server_test.cc
)

target_include_directories(no_server_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(no_server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(num_external_connectivity_watchers_test
  test/core/surface/num_external_connectivity_watchers_test.cc
)

target_include_directories(num_external_connectivity_watchers_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(num_external_connectivity_watchers_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(parse_address_test
  test/core/address_utils/parse_address_test.cc
)

target_include_directories(parse_address_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(parse_address_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(parse_address_with_named_scope_id_test
    test/core/address_utils/parse_address_with_named_scope_id_test.cc
  )

  target_include_directories(parse_address_with_named_scope_id_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(parse_address_with_named_scope_id_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(parser_test
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/core/http/parser_test.cc
)

target_include_directories(parser_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(parser_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(percent_encoding_test
  test/core/slice/percent_encoding_test.cc
)

target_include_directories(percent_encoding_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(percent_encoding_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(public_headers_must_be_c89
  test/core/surface/public_headers_must_be_c89.c
)

target_include_directories(public_headers_must_be_c89
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(public_headers_must_be_c89
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(resolve_address_using_ares_resolver_posix_test
    test/core/iomgr/resolve_address_posix_test.cc
  )

  target_include_directories(resolve_address_using_ares_resolver_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(resolve_address_using_ares_resolver_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(resolve_address_using_ares_resolver_test
  test/core/iomgr/resolve_address_test.cc
)

target_include_directories(resolve_address_using_ares_resolver_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(resolve_address_using_ares_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(resolve_address_using_native_resolver_posix_test
    test/core/iomgr/resolve_address_posix_test.cc
  )

  target_include_directories(resolve_address_using_native_resolver_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(resolve_address_using_native_resolver_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(resolve_address_using_native_resolver_test
  test/core/iomgr/resolve_address_test.cc
)

target_include_directories(resolve_address_using_native_resolver_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(resolve_address_using_native_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(resource_quota_test
  test/core/iomgr/resource_quota_test.cc
)

target_include_directories(resource_quota_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(resource_quota_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(secure_channel_create_test
  test/core/surface/secure_channel_create_test.cc
)

target_include_directories(secure_channel_create_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(secure_channel_create_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(secure_endpoint_test
  test/core/iomgr/endpoint_tests.cc
  test/core/security/secure_endpoint_test.cc
)

target_include_directories(secure_endpoint_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(secure_endpoint_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(security_connector_test
  test/core/security/security_connector_test.cc
)

target_include_directories(security_connector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(security_connector_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(sequential_connectivity_test
  test/core/surface/sequential_connectivity_test.cc
)

target_include_directories(sequential_connectivity_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(sequential_connectivity_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(server_ssl_test
    test/core/handshake/server_ssl.cc
    test/core/handshake/server_ssl_common.cc
  )

  target_include_directories(server_ssl_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(server_ssl_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(server_test
  test/core/surface/server_test.cc
)

target_include_directories(server_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(slice_buffer_test
  test/core/slice/slice_buffer_test.cc
)

target_include_directories(slice_buffer_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(slice_buffer_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(slice_string_helpers_test
  test/core/slice/slice_string_helpers_test.cc
)

target_include_directories(slice_string_helpers_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(slice_string_helpers_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(sockaddr_resolver_test
  test/core/client_channel/resolvers/sockaddr_resolver_test.cc
)

target_include_directories(sockaddr_resolver_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(sockaddr_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(socket_utils_test
    test/core/iomgr/socket_utils_test.cc
  )

  target_include_directories(socket_utils_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(socket_utils_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(spinlock_test
  test/core/gpr/spinlock_test.cc
)

target_include_directories(spinlock_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(spinlock_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(ssl_credentials_test
  test/core/security/ssl_credentials_test.cc
)

target_include_directories(ssl_credentials_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(ssl_credentials_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(ssl_transport_security_test
    test/core/tsi/ssl_transport_security_test.cc
    test/core/tsi/transport_security_test_lib.cc
  )

  target_include_directories(ssl_transport_security_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(ssl_transport_security_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(status_conversion_test
  test/core/transport/status_conversion_test.cc
)

target_include_directories(status_conversion_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(status_conversion_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(stream_compression_test
  test/core/compression/stream_compression_test.cc
)

target_include_directories(stream_compression_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(stream_compression_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(stream_map_test
  test/core/transport/chttp2/stream_map_test.cc
)

target_include_directories(stream_map_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(stream_map_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(stream_owned_slice_test
  test/core/transport/stream_owned_slice_test.cc
)

target_include_directories(stream_owned_slice_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(stream_owned_slice_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(string_test
  test/core/gpr/string_test.cc
)

target_include_directories(string_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(string_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(sync_test
  test/core/gpr/sync_test.cc
)

target_include_directories(sync_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(sync_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(tcp_client_posix_test
    test/core/iomgr/tcp_client_posix_test.cc
  )

  target_include_directories(tcp_client_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(tcp_client_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(tcp_posix_test
    test/core/iomgr/endpoint_tests.cc
    test/core/iomgr/tcp_posix_test.cc
  )

  target_include_directories(tcp_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(tcp_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(tcp_server_posix_test
    test/core/iomgr/tcp_server_posix_test.cc
  )

  target_include_directories(tcp_server_posix_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(tcp_server_posix_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(test_core_gpr_time_test
  test/core/gpr/time_test.cc
)

target_include_directories(test_core_gpr_time_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(test_core_gpr_time_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_core_security_credentials_test
  test/core/security/credentials_test.cc
)

target_include_directories(test_core_security_credentials_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(test_core_security_credentials_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_core_slice_slice_test
  test/core/slice/slice_test.cc
)

target_include_directories(test_core_slice_slice_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(test_core_slice_slice_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(thd_test
  test/core/gprpp/thd_test.cc
)

target_include_directories(thd_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(thd_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(threadpool_test
  test/core/iomgr/threadpool_test.cc
)

target_include_directories(threadpool_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(threadpool_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(time_averaged_stats_test
  test/core/iomgr/time_averaged_stats_test.cc
)

target_include_directories(time_averaged_stats_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(time_averaged_stats_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(timeout_encoding_test
  test/core/transport/timeout_encoding_test.cc
)

target_include_directories(timeout_encoding_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(timeout_encoding_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(timer_heap_test
  test/core/iomgr/timer_heap_test.cc
)

target_include_directories(timer_heap_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(timer_heap_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(timer_list_test
  test/core/iomgr/timer_list_test.cc
)

target_include_directories(timer_list_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(timer_list_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(tls_test
  test/core/gpr/tls_test.cc
)

target_include_directories(tls_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(tls_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(transport_security_common_api_test
  test/core/tsi/alts/handshaker/transport_security_common_api_test.cc
)

target_include_directories(transport_security_common_api_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(transport_security_common_api_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(transport_security_test
  test/core/tsi/transport_security_test.cc
)

target_include_directories(transport_security_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(transport_security_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(udp_server_test
    test/core/iomgr/udp_server_test.cc
  )

  target_include_directories(udp_server_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
  )

  target_link_libraries(udp_server_test
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(useful_test
  test/core/gpr/useful_test.cc
)

target_include_directories(useful_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(useful_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(varint_test
  test/core/transport/chttp2/varint_test.cc
)

target_include_directories(varint_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)

target_link_libraries(varint_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(address_sorting_test
    test/cpp/naming/address_sorting_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(address_sorting_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(address_sorting_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_config
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(address_sorting_test_unsecure
    test/cpp/naming/address_sorting_test.cc
    test/cpp/util/byte_buffer_proto_helper.cc
    test/cpp/util/string_ref_helper.cc
    test/cpp/util/subprocess.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(address_sorting_test_unsecure
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(address_sorting_test_unsecure
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_unsecure
    grpc_test_util_unsecure
    grpc++_test_config
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(admin_services_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.h
  src/cpp/server/admin/admin_services.cc
  src/cpp/server/csds/csds.cc
  test/cpp/end2end/admin_services_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(admin_services_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(admin_services_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_reflection
  grpcpp_channelz
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(alarm_test
    test/cpp/common/alarm_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(alarm_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(alarm_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_unsecure
    grpc_test_util_unsecure
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(alts_concurrent_connectivity_test
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/handshaker.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/handshaker.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/handshaker.pb.h
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/handshaker.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/transport_security_common.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/transport_security_common.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/transport_security_common.pb.h
    ${_gRPC_PROTO_GENS_DIR}/test/core/tsi/alts/fake_handshaker/transport_security_common.grpc.pb.h
    test/core/end2end/cq_verifier.cc
    test/core/tsi/alts/fake_handshaker/fake_handshaker_server.cc
    test/core/tsi/alts/handshaker/alts_concurrent_connectivity_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(alts_concurrent_connectivity_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(alts_concurrent_connectivity_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_util_test
  test/cpp/common/alts_util_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(alts_util_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_util_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_alts
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(async_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/async_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(async_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(async_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(auth_property_iterator_test
  test/cpp/common/auth_property_iterator_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(auth_property_iterator_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(auth_property_iterator_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(authorization_matchers_test
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  test/core/security/authorization_matchers_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(authorization_matchers_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(authorization_matchers_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(aws_request_signer_test
  test/core/security/aws_request_signer_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(aws_request_signer_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(aws_request_signer_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(backoff_test
  test/core/backoff/backoff_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(backoff_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(backoff_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(bad_streaming_id_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/bad_streaming_id.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(bad_streaming_id_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bad_streaming_id_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(badreq_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/badreq.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(badreq_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(badreq_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(bdp_estimator_test
    test/core/transport/bdp_estimator_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bdp_estimator_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bdp_estimator_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_alarm
    test/cpp/microbenchmarks/bm_alarm.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_alarm
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_alarm
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_arena
    test/cpp/microbenchmarks/bm_arena.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_arena
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_arena
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_byte_buffer
    test/cpp/microbenchmarks/bm_byte_buffer.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_byte_buffer
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_byte_buffer
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_call_create
    test/cpp/microbenchmarks/bm_call_create.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_call_create
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_call_create
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_callback_streaming_ping_pong
    test/cpp/microbenchmarks/bm_callback_streaming_ping_pong.cc
    test/cpp/microbenchmarks/callback_test_service.cc
    test/cpp/util/byte_buffer_proto_helper.cc
    test/cpp/util/string_ref_helper.cc
    test/cpp/util/subprocess.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_callback_streaming_ping_pong
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_callback_streaming_ping_pong
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_callback_unary_ping_pong
    test/cpp/microbenchmarks/bm_callback_unary_ping_pong.cc
    test/cpp/microbenchmarks/callback_test_service.cc
    test/cpp/util/byte_buffer_proto_helper.cc
    test/cpp/util/string_ref_helper.cc
    test/cpp/util/subprocess.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_callback_unary_ping_pong
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_callback_unary_ping_pong
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_channel
    test/cpp/microbenchmarks/bm_channel.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_channel
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_channel
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_chttp2_hpack
    test/cpp/microbenchmarks/bm_chttp2_hpack.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_chttp2_hpack
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_chttp2_hpack
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_chttp2_transport
    test/cpp/microbenchmarks/bm_chttp2_transport.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_chttp2_transport
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_chttp2_transport
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_closure
    test/cpp/microbenchmarks/bm_closure.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_closure
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_closure
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_cq
    test/cpp/microbenchmarks/bm_cq.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_cq
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_cq
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_cq_multiple_threads
    test/cpp/microbenchmarks/bm_cq_multiple_threads.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_cq_multiple_threads
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_cq_multiple_threads
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_error
    test/cpp/microbenchmarks/bm_error.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_error
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_error
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_fullstack_streaming_ping_pong
    test/cpp/microbenchmarks/bm_fullstack_streaming_ping_pong.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_fullstack_streaming_ping_pong
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_fullstack_streaming_ping_pong
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_fullstack_streaming_pump
    test/cpp/microbenchmarks/bm_fullstack_streaming_pump.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_fullstack_streaming_pump
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_fullstack_streaming_pump
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_fullstack_trickle
    test/cpp/microbenchmarks/bm_fullstack_trickle.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_fullstack_trickle
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_fullstack_trickle
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    absl::flags
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_fullstack_unary_ping_pong
    test/cpp/microbenchmarks/bm_fullstack_unary_ping_pong.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_fullstack_unary_ping_pong
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_fullstack_unary_ping_pong
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_metadata
    test/cpp/microbenchmarks/bm_metadata.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_metadata
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_metadata
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_pollset
    test/cpp/microbenchmarks/bm_pollset.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_pollset
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_pollset
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_threadpool
    test/cpp/microbenchmarks/bm_threadpool.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_threadpool
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_threadpool
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)

  add_executable(bm_timer
    test/cpp/microbenchmarks/bm_timer.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(bm_timer
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(bm_timer
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    benchmark_helpers
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(byte_buffer_test
  test/cpp/util/byte_buffer_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(byte_buffer_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(byte_buffer_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(byte_stream_test
  test/core/transport/byte_stream_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(byte_stream_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(byte_stream_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(cancel_ares_query_test
  test/core/end2end/cq_verifier.cc
  test/cpp/naming/cancel_ares_query_test.cc
  test/cpp/naming/dns_test_util.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(cancel_ares_query_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cancel_ares_query_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(cel_authorization_engine_test
  src/core/lib/security/authorization/cel_authorization_engine.cc
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  test/core/security/cel_authorization_engine_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(cel_authorization_engine_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cel_authorization_engine_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flat_hash_set
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(certificate_provider_registry_test
  test/core/client_channel/certificate_provider_registry_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(certificate_provider_registry_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(certificate_provider_registry_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(certificate_provider_store_test
  test/core/xds/certificate_provider_store_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(certificate_provider_store_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(certificate_provider_store_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(cfstream_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/cfstream_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(cfstream_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cfstream_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_arguments_test
  test/cpp/common/channel_arguments_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channel_arguments_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_arguments_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_filter_test
  test/cpp/common/channel_filter_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channel_filter_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_filter_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channel_trace_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  test/core/channel/channel_trace_test.cc
  test/cpp/util/channel_trace_proto_helper.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channel_trace_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_trace_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channelz_registry_test
  test/core/channel/channelz_registry_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channelz_registry_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_registry_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channelz_service_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/channelz_service_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channelz_service_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_service_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpcpp_channelz
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(channelz_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  test/core/channel/channelz_test.cc
  test/cpp/util/channel_trace_proto_helper.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(channelz_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(cli_call_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/util/cli_call.cc
  test/cpp/util/cli_call_test.cc
  test/cpp/util/cli_credentials.cc
  test/cpp/util/grpc_tool.cc
  test/cpp/util/proto_file_parser.cc
  test/cpp/util/proto_reflection_descriptor_database.cc
  test/cpp/util/service_describer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(cli_call_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cli_call_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(client_callback_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/client_callback_end2end_test.cc
  test/cpp/end2end/interceptors_util.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(client_callback_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_callback_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(client_channel_stress_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/client/client_channel_stress_test.cc
    test/cpp/end2end/test_service_impl.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(client_channel_stress_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(client_channel_stress_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(client_interceptors_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/client_interceptors_end2end_test.cc
  test/cpp/end2end/interceptors_util.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(client_interceptors_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_interceptors_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(client_lb_end2end_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/orca_load_report_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/orca_load_report_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/orca_load_report_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/orca_load_report_for_test.grpc.pb.h
    test/core/util/test_lb_policies.cc
    test/cpp/end2end/client_lb_end2end_test.cc
    test/cpp/end2end/test_service_impl.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(client_lb_end2end_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(client_lb_end2end_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(codegen_test_full
  test/cpp/codegen/codegen_test_full.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(codegen_test_full
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(codegen_test_full
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(codegen_test_minimal
  test/cpp/codegen/codegen_test_minimal.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(codegen_test_minimal
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(codegen_test_minimal
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(connection_prefix_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/connection_prefix.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(connection_prefix_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(connection_prefix_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(connectivity_state_test
  test/core/transport/connectivity_state_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(connectivity_state_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(connectivity_state_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(context_allocator_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/context_allocator_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(context_allocator_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(context_allocator_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(context_list_test
  test/core/transport/chttp2/context_list_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(context_list_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(context_list_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(delegating_channel_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/delegating_channel_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(delegating_channel_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(delegating_channel_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(destroy_grpclb_channel_with_active_connect_stress_test
  test/cpp/client/destroy_grpclb_channel_with_active_connect_stress_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(destroy_grpclb_channel_with_active_connect_stress_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(destroy_grpclb_channel_with_active_connect_stress_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(dual_ref_counted_test
  test/core/gprpp/dual_ref_counted_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(dual_ref_counted_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(dual_ref_counted_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(duplicate_header_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/duplicate_header.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(duplicate_header_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(duplicate_header_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/end2end_test.cc
  test/cpp/end2end/interceptors_util.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(error_details_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  test/cpp/util/error_details_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(error_details_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(error_details_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_error_details
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(error_utils_test
  test/core/transport/error_utils_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(error_utils_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(error_utils_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(evaluate_args_test
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  test/core/security/evaluate_args_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(evaluate_args_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(evaluate_args_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(examine_stack_test
    test/core/gprpp/examine_stack_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(examine_stack_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(examine_stack_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(exception_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/exception_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(exception_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(exception_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(file_watcher_certificate_provider_factory_test
  test/core/xds/file_watcher_certificate_provider_factory_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(file_watcher_certificate_provider_factory_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(file_watcher_certificate_provider_factory_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(filter_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/filter_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(filter_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(filter_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(flaky_network_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/flaky_network_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(flaky_network_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(flaky_network_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(generic_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/generic_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(generic_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(generic_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(global_config_env_test
    test/core/gprpp/global_config_env_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(global_config_env_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(global_config_env_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(global_config_test
  test/core/gprpp/global_config_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(global_config_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(global_config_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(google_mesh_ca_certificate_provider_factory_test
  src/core/ext/xds/google_mesh_ca_certificate_provider_factory.cc
  test/core/xds/google_mesh_ca_certificate_provider_factory_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(google_mesh_ca_certificate_provider_factory_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(google_mesh_ca_certificate_provider_factory_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_authorization_engine_test
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  test/core/security/grpc_authorization_engine_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpc_authorization_engine_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_authorization_engine_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_cli
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
  test/cpp/util/cli_call.cc
  test/cpp/util/cli_credentials.cc
  test/cpp/util/grpc_cli.cc
  test/cpp/util/grpc_tool.cc
  test/cpp/util/proto_file_parser.cc
  test/cpp/util/proto_reflection_descriptor_database.cc
  test/cpp/util/service_describer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpc_cli
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cli
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc++
  grpc++_test_config
)


endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_CPP_PLUGIN)

add_executable(grpc_cpp_plugin
  src/compiler/cpp_plugin.cc
)

target_include_directories(grpc_cpp_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cpp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_cpp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_CSHARP_PLUGIN)

add_executable(grpc_csharp_plugin
  src/compiler/csharp_plugin.cc
)

target_include_directories(grpc_csharp_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_csharp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_csharp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_NODE_PLUGIN)

add_executable(grpc_node_plugin
  src/compiler/node_plugin.cc
)

target_include_directories(grpc_node_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_node_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_node_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN)

add_executable(grpc_objective_c_plugin
  src/compiler/objective_c_plugin.cc
)

target_include_directories(grpc_objective_c_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_objective_c_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_objective_c_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_PHP_PLUGIN)

add_executable(grpc_php_plugin
  src/compiler/php_plugin.cc
)

target_include_directories(grpc_php_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_php_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_php_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_PYTHON_PLUGIN)

add_executable(grpc_python_plugin
  src/compiler/python_plugin.cc
)

target_include_directories(grpc_python_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_python_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_python_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_RUBY_PLUGIN)

add_executable(grpc_ruby_plugin
  src/compiler/ruby_plugin.cc
)

target_include_directories(grpc_ruby_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_ruby_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_ruby_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_tls_certificate_distributor_test
  test/core/security/grpc_tls_certificate_distributor_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpc_tls_certificate_distributor_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_tls_certificate_distributor_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_tls_certificate_provider_test
  test/core/security/grpc_tls_certificate_provider_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpc_tls_certificate_provider_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_tls_certificate_provider_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(grpc_tls_credentials_options_test
  test/core/security/grpc_tls_credentials_options_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpc_tls_credentials_options_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_tls_credentials_options_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(grpc_tool_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/util/cli_call.cc
    test/cpp/util/cli_credentials.cc
    test/cpp/util/grpc_tool.cc
    test/cpp/util/grpc_tool_test.cc
    test/cpp/util/proto_file_parser.cc
    test/cpp/util/proto_reflection_descriptor_database.cc
    test/cpp/util/service_describer.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(grpc_tool_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(grpc_tool_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_reflection
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(grpclb_api_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
  test/cpp/grpclb/grpclb_api_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(grpclb_api_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpclb_api_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(grpclb_end2end_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/end2end/grpclb_end2end_test.cc
    test/cpp/end2end/test_service_impl.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(grpclb_end2end_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(grpclb_end2end_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(h2_ssl_session_reuse_test
  test/core/end2end/h2_ssl_session_reuse_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(h2_ssl_session_reuse_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(h2_ssl_session_reuse_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(head_of_line_blocking_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/head_of_line_blocking.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(head_of_line_blocking_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(head_of_line_blocking_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(headers_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/headers.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(headers_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(headers_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(health_service_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/health_service_end2end_test.cc
  test/cpp/end2end/test_health_check_service_impl.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(health_service_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(health_service_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(http2_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/cpp/interop/http2_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(http2_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http2_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(hybrid_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/hybrid_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(hybrid_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(hybrid_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(init_test
  test/core/surface/init_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(init_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(init_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(initial_settings_frame_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/initial_settings_frame.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(initial_settings_frame_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(initial_settings_frame_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(insecure_security_connector_test
  test/core/security/insecure_security_connector_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(insecure_security_connector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(insecure_security_connector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(interop_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/core/security/oauth2_utils.cc
  test/cpp/interop/client.cc
  test/cpp/interop/client_helper.cc
  test/cpp/interop/interop_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(interop_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(interop_server
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/cpp/interop/interop_server.cc
  test/cpp/interop/interop_server_bootstrap.cc
  test/cpp/interop/server_helper.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(interop_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(interop_test
    test/cpp/interop/interop_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(interop_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(interop_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_config
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(json_test
  test/core/json/json_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(json_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(json_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(large_metadata_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/large_metadata.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(large_metadata_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(large_metadata_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(lb_get_cpu_stats_test
  src/cpp/server/load_reporter/get_cpu_stats_linux.cc
  src/cpp/server/load_reporter/get_cpu_stats_macos.cc
  src/cpp/server/load_reporter/get_cpu_stats_unsupported.cc
  src/cpp/server/load_reporter/get_cpu_stats_windows.cc
  test/cpp/server/load_reporter/get_cpu_stats_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(lb_get_cpu_stats_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(lb_get_cpu_stats_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(lb_load_data_store_test
  src/cpp/server/load_reporter/load_data_store.cc
  test/cpp/server/load_reporter/load_data_store_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(lb_load_data_store_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(lb_load_data_store_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(linux_system_roots_test
  test/core/security/linux_system_roots_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(linux_system_roots_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(linux_system_roots_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(log_test
  test/core/gpr/log_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(log_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(log_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(matchers_test
  test/core/security/matchers_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(matchers_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(matchers_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(message_allocator_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/message_allocator_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(message_allocator_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(message_allocator_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(mock_stream_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/test/mock_stream_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(mock_stream_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(mock_stream_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(mock_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/mock_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(mock_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(mock_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(nonblocking_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/nonblocking_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(nonblocking_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(nonblocking_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(noop-benchmark
  test/cpp/microbenchmarks/noop-benchmark.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(noop-benchmark
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(noop-benchmark
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(orphanable_test
  test/core/gprpp/orphanable_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(orphanable_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(orphanable_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(out_of_bounds_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/out_of_bounds.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(out_of_bounds_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(out_of_bounds_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(pid_controller_test
  test/core/transport/pid_controller_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(pid_controller_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(pid_controller_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(port_sharing_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/port_sharing_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(port_sharing_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(port_sharing_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(proto_server_reflection_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/proto_server_reflection_test.cc
  test/cpp/end2end/test_service_impl.cc
  test/cpp/util/proto_reflection_descriptor_database.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(proto_server_reflection_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(proto_server_reflection_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_reflection
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(proto_utils_test
  test/cpp/codegen/proto_utils_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(proto_utils_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(proto_utils_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(qps_json_driver
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  src/cpp/util/core_stats.cc
  test/cpp/qps/benchmark_config.cc
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_callback.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/driver.cc
  test/cpp/qps/parse_json.cc
  test/cpp/qps/qps_json_driver.cc
  test/cpp/qps/qps_server_builder.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/report.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_callback.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(qps_json_driver
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_json_driver
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(qps_worker
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  src/cpp/util/core_stats.cc
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_callback.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/qps_server_builder.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_callback.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
  test/cpp/qps/worker.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(qps_worker
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_worker
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(raw_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/raw_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(raw_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(raw_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(rbac_translator_test
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  src/core/lib/security/authorization/rbac_translator.cc
  test/core/security/rbac_translator_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(rbac_translator_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(rbac_translator_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(ref_counted_ptr_test
  test/core/gprpp/ref_counted_ptr_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(ref_counted_ptr_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(ref_counted_ptr_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(ref_counted_test
  test/core/gprpp/ref_counted_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(ref_counted_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(ref_counted_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(remove_stream_from_stalled_lists_test
    test/core/transport/chttp2/remove_stream_from_stalled_lists_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(remove_stream_from_stalled_lists_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(remove_stream_from_stalled_lists_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(retry_throttle_test
  test/core/client_channel/retry_throttle_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(retry_throttle_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(retry_throttle_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(secure_auth_context_test
  test/cpp/common/secure_auth_context_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(secure_auth_context_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(secure_auth_context_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_builder_plugin_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/server_builder_plugin_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_builder_plugin_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_builder_plugin_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(server_builder_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/server/server_builder_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(server_builder_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(server_builder_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_unsecure
    grpc_test_util_unsecure
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(server_builder_with_socket_mutator_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/server/server_builder_with_socket_mutator_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(server_builder_with_socket_mutator_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(server_builder_with_socket_mutator_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_unsecure
    grpc_test_util_unsecure
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(server_chttp2_test
  test/core/surface/server_chttp2_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_chttp2_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_chttp2_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_context_test_spouse_test
  test/cpp/test/server_context_test_spouse_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_context_test_spouse_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_context_test_spouse_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_early_return_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/server_early_return_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_early_return_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_early_return_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_interceptors_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/interceptors_util.cc
  test/cpp/end2end/server_interceptors_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_interceptors_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_interceptors_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_registered_method_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/server_registered_method.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_registered_method_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_registered_method_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(server_request_call_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/server/server_request_call_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(server_request_call_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(server_request_call_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_unsecure
    grpc_test_util_unsecure
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(service_config_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/service_config_end2end_test.cc
  test/cpp/end2end/test_service_impl.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(service_config_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(service_config_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(service_config_test
  test/core/client_channel/service_config_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(service_config_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(service_config_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(settings_timeout_test
  test/core/transport/chttp2/settings_timeout_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(settings_timeout_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(settings_timeout_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(shutdown_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/shutdown_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(shutdown_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(shutdown_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(simple_request_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/simple_request.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(simple_request_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(simple_request_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(sockaddr_utils_test
  test/core/address_utils/sockaddr_utils_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(sockaddr_utils_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(sockaddr_utils_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(stack_tracer_test
    test/core/util/stack_tracer_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(stack_tracer_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(stack_tracer_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(stat_test
  test/core/gprpp/stat_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(stat_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(stat_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(static_metadata_test
  test/core/transport/static_metadata_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(static_metadata_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(static_metadata_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(stats_test
  test/core/debug/stats_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(stats_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(stats_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(status_helper_test
  test/core/gprpp/status_helper_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(status_helper_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(status_helper_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(status_metadata_test
  test/core/transport/status_metadata_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(status_metadata_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(status_metadata_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(status_util_test
  test/core/channel/status_util_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(status_util_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(status_util_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(stranded_event_test
    test/core/end2end/cq_verifier.cc
    test/core/iomgr/stranded_event_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(stranded_event_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(stranded_event_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(streaming_throughput_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/end2end/streaming_throughput_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(streaming_throughput_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(streaming_throughput_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(string_ref_test
  test/cpp/util/string_ref_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(string_ref_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(string_ref_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_cpp_client_credentials_test
  test/cpp/client/credentials_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(test_cpp_client_credentials_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(test_cpp_client_credentials_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_cpp_server_credentials_test
  test/cpp/server/credentials_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(test_cpp_server_credentials_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(test_cpp_server_credentials_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_cpp_util_slice_test
  test/cpp/util/slice_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(test_cpp_util_slice_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(test_cpp_util_slice_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(test_cpp_util_time_test
  test/cpp/util/time_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(test_cpp_util_time_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(test_cpp_util_time_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(thread_manager_test
  test/cpp/thread_manager/thread_manager_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(thread_manager_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(thread_manager_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(thread_stress_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/cpp/end2end/thread_stress_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(thread_stress_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(thread_stress_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(time_jump_test
    test/cpp/common/time_jump_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(time_jump_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(time_jump_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(time_util_test
  test/core/gprpp/time_util_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(time_util_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(time_util_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(timer_test
  test/cpp/common/timer_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(timer_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(timer_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(tls_security_connector_test
  test/core/security/tls_security_connector_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(tls_security_connector_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(tls_security_connector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(too_many_pings_test
  test/core/end2end/cq_verifier.cc
  test/core/transport/chttp2/too_many_pings_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(too_many_pings_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(too_many_pings_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(unknown_frame_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/unknown_frame.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(unknown_frame_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(unknown_frame_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(uri_parser_test
  test/core/uri/uri_parser_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(uri_parser_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(uri_parser_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(window_overflow_bad_client_test
  test/core/bad_client/bad_client.cc
  test/core/bad_client/tests/window_overflow.cc
  test/core/end2end/cq_verifier.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(window_overflow_bad_client_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(window_overflow_bad_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(work_serializer_test
    test/core/iomgr/work_serializer_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(work_serializer_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(work_serializer_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(writes_per_rpc_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    test/core/util/cmdline.cc
    test/core/util/fuzzer_util.cc
    test/core/util/grpc_profiler.cc
    test/core/util/histogram.cc
    test/core/util/memory_counters.cc
    test/core/util/mock_endpoint.cc
    test/core/util/parse_hexstring.cc
    test/core/util/passthru_endpoint.cc
    test/core/util/port.cc
    test/core/util/port_isolated_runtime_environment.cc
    test/core/util/port_server_client.cc
    test/core/util/reconnect_server.cc
    test/core/util/resolve_localhost_ip46.cc
    test/core/util/slice_splitter.cc
    test/core/util/stack_tracer.cc
    test/core/util/subprocess_posix.cc
    test/core/util/subprocess_windows.cc
    test/core/util/test_config.cc
    test/core/util/test_tcp_server.cc
    test/core/util/tracer_util.cc
    test/core/util/trickle_endpoint.cc
    test/cpp/performance/writes_per_rpc_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(writes_per_rpc_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(writes_per_rpc_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    absl::failure_signal_handler
    absl::stacktrace
    absl::symbolize
    grpc++
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_bootstrap_test
  test/core/xds/xds_bootstrap_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_bootstrap_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_bootstrap_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_certificate_provider_test
  test/core/xds/xds_certificate_provider_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_certificate_provider_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_certificate_provider_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_credentials_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  test/cpp/end2end/test_service_impl.cc
  test/cpp/end2end/xds_credentials_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_credentials_end2end_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_credentials_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_credentials_test
  test/core/security/xds_credentials_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_credentials_test
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_credentials_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
)


endif()
if(gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

  add_executable(xds_end2end_test
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/ads_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/ads_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/ads_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/ads_for_test.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/cds_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/cds_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/cds_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/cds_for_test.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/eds_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/eds_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/eds_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/eds_for_test.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lds_rds_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lds_rds_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lds_rds_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lds_rds_for_test.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lrs_for_test.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lrs_for_test.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lrs_for_test.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/lrs_for_test.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/address.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/address.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/address.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/address.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/ads.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/ads.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/ads.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/ads.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/aggregate_cluster.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/aggregate_cluster.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/aggregate_cluster.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/cluster.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/cluster.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/cluster.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_source.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_source.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_source.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_source.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/discovery.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/discovery.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/discovery.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/discovery.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/endpoint.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/endpoint.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/endpoint.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault_common.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault_common.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault_common.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/fault_common.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/http_connection_manager.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/http_connection_manager.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/listener.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/listener.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/listener.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/listener.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/load_report.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/load_report.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/load_report.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/load_report.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/lrs.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/lrs.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/lrs.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/lrs.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/protocol.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/protocol.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/protocol.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/protocol.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/range.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/range.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/range.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/range.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/regex.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/regex.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/regex.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/regex.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/route.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/route.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/route.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/route.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/router.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/router.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/router.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/router.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/string.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/string.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/string.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/string.grpc.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/tls.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/tls.grpc.pb.cc
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/tls.pb.h
    ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/tls.grpc.pb.h
    src/cpp/server/csds/csds.cc
    test/cpp/end2end/test_service_impl.cc
    test/cpp/end2end/xds_end2end_test.cc
    third_party/googletest/googletest/src/gtest-all.cc
    third_party/googletest/googlemock/src/gmock-all.cc
  )

  target_include_directories(xds_end2end_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
      ${_gRPC_RE2_INCLUDE_DIR}
      ${_gRPC_SSL_INCLUDE_DIR}
      ${_gRPC_UPB_GENERATED_DIR}
      ${_gRPC_UPB_GRPC_GENERATED_DIR}
      ${_gRPC_UPB_INCLUDE_DIR}
      ${_gRPC_XXHASH_INCLUDE_DIR}
      ${_gRPC_ZLIB_INCLUDE_DIR}
      third_party/googletest/googletest/include
      third_party/googletest/googletest
      third_party/googletest/googlemock/include
      third_party/googletest/googlemock
      ${_gRPC_PROTO_GENS_DIR}
  )

  target_link_libraries(xds_end2end_test
    ${_gRPC_PROTOBUF_LIBRARIES}
    ${_gRPC_ALLTARGETS_LIBRARIES}
    grpc++_test_util
  )


endif()
endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_interop_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.h
  src/cpp/server/admin/admin_services.cc
  src/cpp/server/csds/csds.cc
  test/cpp/interop/xds_interop_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_interop_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_interop_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc++_reflection
  grpcpp_channelz
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(xds_interop_server
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/base.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/config_dump.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/csds.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/xds/v3/percent.grpc.pb.h
  src/cpp/server/admin/admin_services.cc
  src/cpp/server/csds/csds.cc
  test/cpp/end2end/test_health_check_service_impl.cc
  test/cpp/interop/xds_interop_server.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(xds_interop_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(xds_interop_server
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc++_reflection
  grpcpp_channelz
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(alts_credentials_fuzzer_one_entry
  test/core/security/alts_credentials_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(alts_credentials_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_credentials_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(client_fuzzer_one_entry
  test/core/end2end/fuzzers/client_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(client_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(hpack_parser_fuzzer_test_one_entry
  test/core/transport/chttp2/hpack_parser_fuzzer_test.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(hpack_parser_fuzzer_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(hpack_parser_fuzzer_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(http_request_fuzzer_test_one_entry
  test/core/http/request_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(http_request_fuzzer_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http_request_fuzzer_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(http_response_fuzzer_test_one_entry
  test/core/http/response_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(http_response_fuzzer_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http_response_fuzzer_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(json_fuzzer_test_one_entry
  test/core/json/fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(json_fuzzer_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(json_fuzzer_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_response_test_one_entry
  test/core/nanopb/fuzzer_response.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(nanopb_fuzzer_response_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(nanopb_fuzzer_response_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_serverlist_test_one_entry
  test/core/nanopb/fuzzer_serverlist.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(nanopb_fuzzer_serverlist_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(nanopb_fuzzer_serverlist_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(percent_decode_fuzzer_one_entry
  test/core/slice/percent_decode_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(percent_decode_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(percent_decode_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(percent_encode_fuzzer_one_entry
  test/core/slice/percent_encode_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(percent_encode_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(percent_encode_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(server_fuzzer_one_entry
  test/core/end2end/fuzzers/server_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(server_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(ssl_server_fuzzer_one_entry
  test/core/security/ssl_server_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(ssl_server_fuzzer_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(ssl_server_fuzzer_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(uri_fuzzer_test_one_entry
  test/core/uri/uri_fuzzer_test.cc
  test/core/util/one_corpus_entry_fuzzer.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

target_include_directories(uri_fuzzer_test_one_entry
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(uri_fuzzer_test_one_entry
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags
  grpc_test_util
  grpc++_test_config
)


endif()







if(gRPC_INSTALL)
  install(EXPORT gRPCTargets
    DESTINATION ${gRPC_INSTALL_CMAKEDIR}
    NAMESPACE gRPC::
  )
endif()

include(CMakePackageConfigHelpers)

configure_file(cmake/gRPCConfig.cmake.in
  gRPCConfig.cmake @ONLY)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/gRPCConfigVersion.cmake
  VERSION ${gRPC_CPP_VERSION}
  COMPATIBILITY AnyNewerVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gRPCConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/gRPCConfigVersion.cmake
  DESTINATION ${gRPC_INSTALL_CMAKEDIR}
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Findc-ares.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Findre2.cmake
  DESTINATION ${gRPC_INSTALL_CMAKEDIR}/modules
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/etc/roots.pem
  DESTINATION ${gRPC_INSTALL_SHAREDIR})

# Function to generate pkg-config files.
function(generate_pkgconfig name description version requires
                            libs libs_private output_filename)
  set(PC_NAME "${name}")
  set(PC_DESCRIPTION "${description}")
  set(PC_VERSION "${version}")
  set(PC_REQUIRES "${requires}")
  set(PC_LIB "${libs}")
  set(PC_LIBS_PRIVATE "${libs_private}")
  set(output_filepath "${grpc_BINARY_DIR}/libs/opt/pkgconfig/${output_filename}")
  configure_file(
    "${grpc_SOURCE_DIR}/cmake/pkg-config-template.pc.in"
    "${output_filepath}"
    @ONLY)
  install(FILES "${output_filepath}"
    DESTINATION "lib/pkgconfig/")
endfunction()

# gpr .pc file
generate_pkgconfig(
  "gpr"
  "gRPC platform support library"
  "${gRPC_CORE_VERSION}"
  "absl_base absl_memory absl_optional absl_status absl_str_format absl_strings absl_synchronization absl_time"
  "-lgpr"
  ""
  "gpr.pc")

# grpc .pc file
generate_pkgconfig(
  "gRPC"
  "high performance general RPC framework"
  "${gRPC_CORE_VERSION}"
  "gpr openssl absl_base absl_bind_front absl_flat_hash_map absl_inlined_vector absl_memory absl_optional absl_status absl_statusor absl_str_format absl_strings absl_synchronization absl_time"
  "-lgrpc -laddress_sorting -lre2 -lupb -lcares -lz"
  ""
  "grpc.pc")

# grpc_unsecure .pc file
generate_pkgconfig(
  "gRPC unsecure"
  "high performance general RPC framework without SSL"
  "${gRPC_CORE_VERSION}"
  "gpr absl_base absl_flat_hash_map absl_inlined_vector absl_memory absl_optional absl_status absl_statusor absl_str_format absl_strings absl_synchronization absl_time"
  "-lgrpc_unsecure"
  ""
  "grpc_unsecure.pc")

# grpc++ .pc file
generate_pkgconfig(
  "gRPC++"
  "C++ wrapper for gRPC"
  "${gRPC_CPP_VERSION}"
  "grpc absl_base absl_bind_front absl_flat_hash_map absl_inlined_vector absl_memory absl_optional absl_status absl_statusor absl_str_format absl_strings absl_synchronization absl_time"
  "-lgrpc++"
  ""
  "grpc++.pc")

# grpc++_unsecure .pc file
generate_pkgconfig(
  "gRPC++ unsecure"
  "C++ wrapper for gRPC without SSL"
  "${gRPC_CPP_VERSION}"
  "grpc_unsecure absl_base absl_flat_hash_map absl_inlined_vector absl_memory absl_optional absl_status absl_statusor absl_str_format absl_strings absl_synchronization absl_time"
  "-lgrpc++_unsecure"
  ""
  "grpc++_unsecure.pc")
