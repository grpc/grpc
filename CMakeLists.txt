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



cmake_minimum_required(VERSION 2.8)

set(PACKAGE_NAME      "grpc")
set(PACKAGE_VERSION   "1.15.0-dev")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} C CXX)

set(gRPC_INSTALL_BINDIR "bin" CACHE STRING "Installation directory for executables")
set(gRPC_INSTALL_LIBDIR "lib" CACHE STRING "Installation directory for libraries")
set(gRPC_INSTALL_INCLUDEDIR "include" CACHE STRING "Installation directory for headers")
set(gRPC_INSTALL_CMAKEDIR "lib/cmake/${PACKAGE_NAME}" CACHE STRING "Installation directory for cmake config files")
set(gRPC_INSTALL_SHAREDIR "share/grpc" CACHE STRING "Installation directory for root certificates")

# Options
option(gRPC_BUILD_TESTS "Build tests" OFF)
option(gRPC_BUILD_CODEGEN "Build codegen" ON)
option(gRPC_BUILD_CSHARP_EXT "Build C# extensions" ON)

set(gRPC_INSTALL_default ON)
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Disable gRPC_INSTALL by default if building as a submodule
  set(gRPC_INSTALL_default OFF)
endif()
set(gRPC_INSTALL ${gRPC_INSTALL_default} CACHE BOOL
    "Generate installation target: gRPC_ZLIB_PROVIDER, gRPC_CARES_PROVIDER, gRPC_SSL_PROVIDER and gRPC_PROTOBUF_PROVIDER must all be \"package\"")

# Providers for third-party dependencies (gRPC_*_PROVIDER properties):
# "module": build the dependency using sources from git submodule (under third_party)
# "package": use cmake's find_package functionality to locate a pre-installed dependency

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_CARES_PROVIDER "module" CACHE STRING "Provider of c-ares library")
set_property(CACHE gRPC_CARES_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PACKAGE_TYPE "" CACHE STRING "Algorithm for searching protobuf package")
set_property(CACHE gRPC_PROTOBUF_PACKAGE_TYPE PROPERTY STRINGS "CONFIG" "MODULE")

set(gRPC_GFLAGS_PROVIDER "module" CACHE STRING "Provider of gflags library")
set_property(CACHE gRPC_GFLAGS_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_BENCHMARK_PROVIDER "module" CACHE STRING "Provider of benchmark library")
set_property(CACHE gRPC_BENCHMARK_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if(UNIX)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(_gRPC_PLATFORM_LINUX ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(_gRPC_PLATFORM_MAC ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    set(_gRPC_PLATFORM_ANDROID ON)
  else()
    set(_gRPC_PLATFORM_POSIX ON)
  endif()
endif()
if(WIN32)
  set(_gRPC_PLATFORM_WINDOWS ON)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

add_definitions(-DPB_FIELD_16BIT)

if (MSVC)
  include(cmake/msvc_static_runtime.cmake)
  add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # needed to compile protobuf
  add_definitions(/wd4065 /wd4506)
  # TODO(jtattermusch): revisit C4267 occurrences throughout the code
  add_definitions(/wd4267)
  # TODO(jtattermusch): needed to build boringssl with VS2017, revisit later
  add_definitions(/wd4987 /wd4774 /wd4819 /wd4996 /wd4619)
endif()

if (gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

include(cmake/zlib.cmake)
include(cmake/cares.cmake)
include(cmake/protobuf.cmake)
include(cmake/ssl.cmake)
include(cmake/gflags.cmake)
include(cmake/benchmark.cmake)
include(cmake/address_sorting.cmake)
include(cmake/nanopb.cmake)

if(NOT MSVC)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=c99")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(_gRPC_PLATFORM_MAC)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m pthread)
elseif(_gRPC_PLATFORM_ANDROID)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m)
elseif(UNIX)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} rt m pthread)
endif()

if(WIN32 AND MSVC)
  set(_gRPC_BASELIB_LIBRARIES wsock32 ws2_32)
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

    add_custom_command(
      OUTPUT "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
      COMMAND ${_gRPC_PROTOBUF_PROTOC_EXECUTABLE}
      ARGS --grpc_out=generate_mock_code=true:${_gRPC_PROTO_GENS_DIR}
           --cpp_out=${_gRPC_PROTO_GENS_DIR}
           --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_gRPC_PROTOBUF_PROTOC} grpc_cpp_plugin
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
      VERBATIM)

      set_source_files_properties("${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"  "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()

add_custom_target(plugins
  DEPENDS
  grpc_cpp_plugin
  grpc_csharp_plugin
  grpc_node_plugin
  grpc_objective_c_plugin
  grpc_php_plugin
  grpc_python_plugin
  grpc_ruby_plugin
)

add_custom_target(tools_c
  DEPENDS
  check_epollexclusive
  grpc_create_jwt
  grpc_print_google_default_creds_token
  grpc_verify_jwt
  gen_hpack_tables
  gen_legal_metadata_characters
  gen_percent_encoding_tables
)

add_custom_target(tools_cxx
  DEPENDS
)

add_custom_target(tools
  DEPENDS tools_c tools_cxx)

if (gRPC_BUILD_TESTS)
add_custom_target(buildtests_c)
add_dependencies(buildtests_c algorithm_test)
add_dependencies(buildtests_c alloc_test)
add_dependencies(buildtests_c alpn_test)
add_dependencies(buildtests_c arena_test)
add_dependencies(buildtests_c avl_test)
add_dependencies(buildtests_c bad_server_response_test)
add_dependencies(buildtests_c bin_decoder_test)
add_dependencies(buildtests_c bin_encoder_test)
add_dependencies(buildtests_c channel_create_test)
add_dependencies(buildtests_c chttp2_hpack_encoder_test)
add_dependencies(buildtests_c chttp2_stream_map_test)
add_dependencies(buildtests_c chttp2_varint_test)
add_dependencies(buildtests_c cmdline_test)
add_dependencies(buildtests_c combiner_test)
add_dependencies(buildtests_c compression_test)
add_dependencies(buildtests_c concurrent_connectivity_test)
add_dependencies(buildtests_c connection_refused_test)
add_dependencies(buildtests_c dns_resolver_connectivity_test)
add_dependencies(buildtests_c dns_resolver_cooldown_test)
add_dependencies(buildtests_c dns_resolver_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c dualstack_socket_test)
endif()
add_dependencies(buildtests_c endpoint_pair_test)
add_dependencies(buildtests_c error_test)
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c ev_epollex_linux_test)
endif()
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c ev_epollsig_linux_test)
endif()
add_dependencies(buildtests_c fake_resolver_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c fake_transport_security_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c fd_conservation_posix_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c fd_posix_test)
endif()
add_dependencies(buildtests_c fling_client)
add_dependencies(buildtests_c fling_server)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c fling_stream_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c fling_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC)
add_dependencies(buildtests_c fork_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c goaway_server_test)
endif()
add_dependencies(buildtests_c gpr_cpu_test)
add_dependencies(buildtests_c gpr_env_test)
add_dependencies(buildtests_c gpr_host_port_test)
add_dependencies(buildtests_c gpr_log_test)
add_dependencies(buildtests_c gpr_manual_constructor_test)
add_dependencies(buildtests_c gpr_mpscq_test)
add_dependencies(buildtests_c gpr_spinlock_test)
add_dependencies(buildtests_c gpr_string_test)
add_dependencies(buildtests_c gpr_sync_test)
add_dependencies(buildtests_c gpr_thd_test)
add_dependencies(buildtests_c gpr_time_test)
add_dependencies(buildtests_c gpr_tls_test)
add_dependencies(buildtests_c gpr_useful_test)
add_dependencies(buildtests_c grpc_auth_context_test)
add_dependencies(buildtests_c grpc_b64_test)
add_dependencies(buildtests_c grpc_byte_buffer_reader_test)
add_dependencies(buildtests_c grpc_channel_args_test)
add_dependencies(buildtests_c grpc_channel_stack_builder_test)
add_dependencies(buildtests_c grpc_channel_stack_test)
add_dependencies(buildtests_c grpc_completion_queue_test)
add_dependencies(buildtests_c grpc_completion_queue_threading_test)
add_dependencies(buildtests_c grpc_credentials_test)
add_dependencies(buildtests_c grpc_fetch_oauth2)
add_dependencies(buildtests_c grpc_ipv6_loopback_available_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c grpc_json_token_test)
endif()
add_dependencies(buildtests_c grpc_jwt_verifier_test)
add_dependencies(buildtests_c grpc_security_connector_test)
add_dependencies(buildtests_c grpc_ssl_credentials_test)
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c handshake_client)
endif()
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c handshake_server)
endif()
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c handshake_server_with_readahead_handshaker)
endif()
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c handshake_verify_peer_options)
endif()
add_dependencies(buildtests_c histogram_test)
add_dependencies(buildtests_c hpack_parser_test)
add_dependencies(buildtests_c hpack_table_test)
add_dependencies(buildtests_c http_parser_test)
add_dependencies(buildtests_c httpcli_format_request_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c httpcli_test)
endif()
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c httpscli_test)
endif()
add_dependencies(buildtests_c init_test)
add_dependencies(buildtests_c invalid_call_argument_test)
add_dependencies(buildtests_c json_rewrite)
add_dependencies(buildtests_c json_rewrite_test)
add_dependencies(buildtests_c json_stream_error_test)
add_dependencies(buildtests_c json_test)
add_dependencies(buildtests_c lame_client_test)
add_dependencies(buildtests_c load_file_test)
add_dependencies(buildtests_c memory_profile_client)
add_dependencies(buildtests_c memory_profile_server)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c memory_profile_test)
endif()
add_dependencies(buildtests_c message_compress_test)
add_dependencies(buildtests_c minimal_stack_is_minimal_test)
add_dependencies(buildtests_c multiple_server_queues_test)
add_dependencies(buildtests_c murmur_hash_test)
add_dependencies(buildtests_c no_server_test)
add_dependencies(buildtests_c num_external_connectivity_watchers_test)
add_dependencies(buildtests_c parse_address_test)
add_dependencies(buildtests_c percent_encoding_test)
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c pollset_set_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c resolve_address_posix_test)
endif()
add_dependencies(buildtests_c resolve_address_test)
add_dependencies(buildtests_c resource_quota_test)
add_dependencies(buildtests_c secure_channel_create_test)
add_dependencies(buildtests_c secure_endpoint_test)
add_dependencies(buildtests_c sequential_connectivity_test)
add_dependencies(buildtests_c server_chttp2_test)
add_dependencies(buildtests_c server_test)
add_dependencies(buildtests_c slice_buffer_test)
add_dependencies(buildtests_c slice_string_helpers_test)
add_dependencies(buildtests_c slice_test)
add_dependencies(buildtests_c sockaddr_resolver_test)
add_dependencies(buildtests_c sockaddr_utils_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c socket_utils_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c ssl_transport_security_test)
endif()
add_dependencies(buildtests_c status_conversion_test)
add_dependencies(buildtests_c stream_compression_test)
add_dependencies(buildtests_c stream_owned_slice_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c tcp_client_posix_test)
endif()
add_dependencies(buildtests_c tcp_client_uv_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c tcp_posix_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c tcp_server_posix_test)
endif()
add_dependencies(buildtests_c tcp_server_uv_test)
add_dependencies(buildtests_c time_averaged_stats_test)
add_dependencies(buildtests_c timeout_encoding_test)
add_dependencies(buildtests_c timer_heap_test)
add_dependencies(buildtests_c timer_list_test)
add_dependencies(buildtests_c transport_connectivity_state_test)
add_dependencies(buildtests_c transport_metadata_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c transport_security_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c udp_server_test)
endif()
add_dependencies(buildtests_c uri_parser_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c wakeup_fd_cv_test)
endif()
add_dependencies(buildtests_c public_headers_must_be_c89)
add_dependencies(buildtests_c badreq_bad_client_test)
add_dependencies(buildtests_c connection_prefix_bad_client_test)
add_dependencies(buildtests_c duplicate_header_bad_client_test)
add_dependencies(buildtests_c head_of_line_blocking_bad_client_test)
add_dependencies(buildtests_c headers_bad_client_test)
add_dependencies(buildtests_c initial_settings_frame_bad_client_test)
add_dependencies(buildtests_c large_metadata_bad_client_test)
add_dependencies(buildtests_c server_registered_method_bad_client_test)
add_dependencies(buildtests_c simple_request_bad_client_test)
add_dependencies(buildtests_c unknown_frame_bad_client_test)
add_dependencies(buildtests_c window_overflow_bad_client_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c bad_ssl_cert_server)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c bad_ssl_cert_test)
endif()
add_dependencies(buildtests_c h2_census_test)
add_dependencies(buildtests_c h2_compress_test)
add_dependencies(buildtests_c h2_fakesec_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c h2_fd_test)
endif()
add_dependencies(buildtests_c h2_full_test)
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c h2_full+pipe_test)
endif()
add_dependencies(buildtests_c h2_full+trace_test)
add_dependencies(buildtests_c h2_full+workarounds_test)
add_dependencies(buildtests_c h2_http_proxy_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c h2_local_test)
endif()
add_dependencies(buildtests_c h2_oauth2_test)
add_dependencies(buildtests_c h2_proxy_test)
add_dependencies(buildtests_c h2_sockpair_test)
add_dependencies(buildtests_c h2_sockpair+trace_test)
add_dependencies(buildtests_c h2_sockpair_1byte_test)
add_dependencies(buildtests_c h2_ssl_test)
add_dependencies(buildtests_c h2_ssl_proxy_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c h2_uds_test)
endif()
add_dependencies(buildtests_c inproc_test)
add_dependencies(buildtests_c h2_census_nosec_test)
add_dependencies(buildtests_c h2_compress_nosec_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c h2_fd_nosec_test)
endif()
add_dependencies(buildtests_c h2_full_nosec_test)
if(_gRPC_PLATFORM_LINUX)
add_dependencies(buildtests_c h2_full+pipe_nosec_test)
endif()
add_dependencies(buildtests_c h2_full+trace_nosec_test)
add_dependencies(buildtests_c h2_full+workarounds_nosec_test)
add_dependencies(buildtests_c h2_http_proxy_nosec_test)
add_dependencies(buildtests_c h2_proxy_nosec_test)
add_dependencies(buildtests_c h2_sockpair_nosec_test)
add_dependencies(buildtests_c h2_sockpair+trace_nosec_test)
add_dependencies(buildtests_c h2_sockpair_1byte_nosec_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_c h2_uds_nosec_test)
endif()
add_dependencies(buildtests_c inproc_nosec_test)
add_dependencies(buildtests_c alts_credentials_fuzzer_one_entry)
add_dependencies(buildtests_c api_fuzzer_one_entry)
add_dependencies(buildtests_c client_fuzzer_one_entry)
add_dependencies(buildtests_c hpack_parser_fuzzer_test_one_entry)
add_dependencies(buildtests_c http_request_fuzzer_test_one_entry)
add_dependencies(buildtests_c http_response_fuzzer_test_one_entry)
add_dependencies(buildtests_c json_fuzzer_test_one_entry)
add_dependencies(buildtests_c nanopb_fuzzer_response_test_one_entry)
add_dependencies(buildtests_c nanopb_fuzzer_serverlist_test_one_entry)
add_dependencies(buildtests_c percent_decode_fuzzer_one_entry)
add_dependencies(buildtests_c percent_encode_fuzzer_one_entry)
add_dependencies(buildtests_c server_fuzzer_one_entry)
add_dependencies(buildtests_c ssl_server_fuzzer_one_entry)
add_dependencies(buildtests_c uri_fuzzer_test_one_entry)

add_custom_target(buildtests_cxx)
add_dependencies(buildtests_cxx alarm_test)
add_dependencies(buildtests_cxx alts_counter_test)
add_dependencies(buildtests_cxx alts_crypt_test)
add_dependencies(buildtests_cxx alts_crypter_test)
add_dependencies(buildtests_cxx alts_frame_handler_test)
add_dependencies(buildtests_cxx alts_frame_protector_test)
add_dependencies(buildtests_cxx alts_grpc_record_protocol_test)
add_dependencies(buildtests_cxx alts_handshaker_client_test)
add_dependencies(buildtests_cxx alts_handshaker_service_api_test)
add_dependencies(buildtests_cxx alts_iovec_record_protocol_test)
add_dependencies(buildtests_cxx alts_security_connector_test)
add_dependencies(buildtests_cxx alts_tsi_handshaker_test)
add_dependencies(buildtests_cxx alts_tsi_utils_test)
add_dependencies(buildtests_cxx alts_zero_copy_grpc_protector_test)
add_dependencies(buildtests_cxx async_end2end_test)
add_dependencies(buildtests_cxx auth_property_iterator_test)
add_dependencies(buildtests_cxx backoff_test)
add_dependencies(buildtests_cxx bdp_estimator_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_arena)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_call_create)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_channel)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_channel_secure)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_chttp2_hpack)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_chttp2_transport)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_closure)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_cq)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_cq_multiple_threads)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_error)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_fullstack_streaming_ping_pong)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_fullstack_streaming_pump)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_fullstack_trickle)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_fullstack_unary_ping_pong)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_metadata)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx bm_pollset)
endif()
add_dependencies(buildtests_cxx byte_stream_test)
add_dependencies(buildtests_cxx channel_arguments_test)
add_dependencies(buildtests_cxx channel_filter_test)
add_dependencies(buildtests_cxx channel_trace_test)
add_dependencies(buildtests_cxx channelz_registry_test)
add_dependencies(buildtests_cxx channelz_service_test)
add_dependencies(buildtests_cxx channelz_test)
add_dependencies(buildtests_cxx check_gcp_environment_linux_test)
add_dependencies(buildtests_cxx check_gcp_environment_windows_test)
add_dependencies(buildtests_cxx chttp2_settings_timeout_test)
add_dependencies(buildtests_cxx cli_call_test)
add_dependencies(buildtests_cxx client_channel_stress_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx client_crash_test)
endif()
add_dependencies(buildtests_cxx client_crash_test_server)
add_dependencies(buildtests_cxx client_lb_end2end_test)
add_dependencies(buildtests_cxx codegen_test_full)
add_dependencies(buildtests_cxx codegen_test_minimal)
add_dependencies(buildtests_cxx credentials_test)
add_dependencies(buildtests_cxx cxx_byte_buffer_test)
add_dependencies(buildtests_cxx cxx_slice_test)
add_dependencies(buildtests_cxx cxx_string_ref_test)
add_dependencies(buildtests_cxx cxx_time_test)
add_dependencies(buildtests_cxx end2end_test)
add_dependencies(buildtests_cxx error_details_test)
add_dependencies(buildtests_cxx exception_test)
add_dependencies(buildtests_cxx filter_end2end_test)
add_dependencies(buildtests_cxx generic_end2end_test)
add_dependencies(buildtests_cxx golden_file_test)
add_dependencies(buildtests_cxx grpc_alts_credentials_options_test)
add_dependencies(buildtests_cxx grpc_cli)
add_dependencies(buildtests_cxx grpc_tool_test)
add_dependencies(buildtests_cxx grpclb_api_test)
add_dependencies(buildtests_cxx grpclb_end2end_test)
add_dependencies(buildtests_cxx h2_ssl_cert_test)
add_dependencies(buildtests_cxx h2_ssl_session_reuse_test)
add_dependencies(buildtests_cxx health_service_end2end_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx http2_client)
endif()
add_dependencies(buildtests_cxx hybrid_end2end_test)
add_dependencies(buildtests_cxx inlined_vector_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx inproc_sync_unary_ping_pong_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx interop_client)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx interop_server)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx interop_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx json_run_localhost)
endif()
add_dependencies(buildtests_cxx memory_test)
add_dependencies(buildtests_cxx metrics_client)
add_dependencies(buildtests_cxx mock_test)
add_dependencies(buildtests_cxx nonblocking_test)
add_dependencies(buildtests_cxx noop-benchmark)
add_dependencies(buildtests_cxx orphanable_test)
add_dependencies(buildtests_cxx proto_server_reflection_test)
add_dependencies(buildtests_cxx proto_utils_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx qps_interarrival_test)
endif()
add_dependencies(buildtests_cxx qps_json_driver)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx qps_openloop_test)
endif()
add_dependencies(buildtests_cxx qps_worker)
add_dependencies(buildtests_cxx raw_end2end_test)
add_dependencies(buildtests_cxx reconnect_interop_client)
add_dependencies(buildtests_cxx reconnect_interop_server)
add_dependencies(buildtests_cxx ref_counted_ptr_test)
add_dependencies(buildtests_cxx ref_counted_test)
add_dependencies(buildtests_cxx retry_throttle_test)
add_dependencies(buildtests_cxx secure_auth_context_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx secure_sync_unary_ping_pong_test)
endif()
add_dependencies(buildtests_cxx server_builder_plugin_test)
add_dependencies(buildtests_cxx server_builder_test)
if(_gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx server_builder_with_socket_mutator_test)
endif()
add_dependencies(buildtests_cxx server_context_test_spouse_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx server_crash_test)
endif()
add_dependencies(buildtests_cxx server_crash_test_client)
add_dependencies(buildtests_cxx server_early_return_test)
add_dependencies(buildtests_cxx server_request_call_test)
add_dependencies(buildtests_cxx shutdown_test)
add_dependencies(buildtests_cxx slice_hash_table_test)
add_dependencies(buildtests_cxx slice_weak_hash_table_test)
add_dependencies(buildtests_cxx stats_test)
add_dependencies(buildtests_cxx status_metadata_test)
add_dependencies(buildtests_cxx status_util_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx streaming_throughput_test)
endif()
add_dependencies(buildtests_cxx stress_test)
add_dependencies(buildtests_cxx thread_manager_test)
add_dependencies(buildtests_cxx thread_stress_test)
add_dependencies(buildtests_cxx transport_pid_controller_test)
add_dependencies(buildtests_cxx transport_security_common_api_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx writes_per_rpc_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx resolver_component_test_unsecure)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx resolver_component_test)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx resolver_component_tests_runner_invoker_unsecure)
endif()
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx resolver_component_tests_runner_invoker)
endif()
add_dependencies(buildtests_cxx address_sorting_test_unsecure)
add_dependencies(buildtests_cxx address_sorting_test)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)
add_dependencies(buildtests_cxx cancel_ares_query_test)
endif()

add_custom_target(buildtests
  DEPENDS buildtests_c buildtests_cxx)
endif (gRPC_BUILD_TESTS)


add_library(address_sorting
  third_party/address_sorting/address_sorting.c
  third_party/address_sorting/address_sorting_posix.c
  third_party/address_sorting/address_sorting_windows.c
)

if(WIN32 AND MSVC)
  set_target_properties(address_sorting PROPERTIES COMPILE_PDB_NAME "address_sorting"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/address_sorting.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(address_sorting
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(address_sorting
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if (gRPC_INSTALL)
  install(TARGETS address_sorting EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(alts_test_util
  test/core/tsi/alts/crypt/gsec_test_util.cc
  test/core/tsi/alts/handshaker/alts_handshaker_service_api_test_lib.cc
)

if(WIN32 AND MSVC)
  set_target_properties(alts_test_util PROPERTIES COMPILE_PDB_NAME "alts_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/alts_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(alts_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(alts_test_util
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
)


endif (gRPC_BUILD_TESTS)

add_library(gpr
  src/core/lib/gpr/alloc.cc
  src/core/lib/gpr/arena.cc
  src/core/lib/gpr/atm.cc
  src/core/lib/gpr/cpu_iphone.cc
  src/core/lib/gpr/cpu_linux.cc
  src/core/lib/gpr/cpu_posix.cc
  src/core/lib/gpr/cpu_windows.cc
  src/core/lib/gpr/env_linux.cc
  src/core/lib/gpr/env_posix.cc
  src/core/lib/gpr/env_windows.cc
  src/core/lib/gpr/host_port.cc
  src/core/lib/gpr/log.cc
  src/core/lib/gpr/log_android.cc
  src/core/lib/gpr/log_linux.cc
  src/core/lib/gpr/log_posix.cc
  src/core/lib/gpr/log_windows.cc
  src/core/lib/gpr/mpscq.cc
  src/core/lib/gpr/murmur_hash.cc
  src/core/lib/gpr/string.cc
  src/core/lib/gpr/string_posix.cc
  src/core/lib/gpr/string_util_windows.cc
  src/core/lib/gpr/string_windows.cc
  src/core/lib/gpr/sync.cc
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
  src/core/lib/gprpp/fork.cc
  src/core/lib/gprpp/thd_posix.cc
  src/core/lib/gprpp/thd_windows.cc
  src/core/lib/profiling/basic_timers.cc
  src/core/lib/profiling/stap_timers.cc
)

if(WIN32 AND MSVC)
  set_target_properties(gpr PROPERTIES COMPILE_PDB_NAME "gpr"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpr.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(gpr
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr
  ${_gRPC_ALLTARGETS_LIBRARIES}
)
if (_gRPC_PLATFORM_ANDROID)
  target_link_libraries(gpr
    android
    log
  )
endif (_gRPC_PLATFORM_ANDROID)

foreach(_hdr
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS gpr EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(gpr_test_util
  test/core/util/test_config.cc
)

if(WIN32 AND MSVC)
  set_target_properties(gpr_test_util PROPERTIES COMPILE_PDB_NAME "gpr_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpr_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(gpr_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_test_util
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
)


endif (gRPC_BUILD_TESTS)

add_library(grpc
  src/core/lib/surface/init.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/lib/http/httpcli_security_connector.cc
  src/core/lib/security/context/security_context.cc
  src/core/lib/security/credentials/alts/alts_credentials.cc
  src/core/lib/security/credentials/composite/composite_credentials.cc
  src/core/lib/security/credentials/credentials.cc
  src/core/lib/security/credentials/credentials_metadata.cc
  src/core/lib/security/credentials/fake/fake_credentials.cc
  src/core/lib/security/credentials/google_default/credentials_generic.cc
  src/core/lib/security/credentials/google_default/google_default_credentials.cc
  src/core/lib/security/credentials/iam/iam_credentials.cc
  src/core/lib/security/credentials/jwt/json_token.cc
  src/core/lib/security/credentials/jwt/jwt_credentials.cc
  src/core/lib/security/credentials/jwt/jwt_verifier.cc
  src/core/lib/security/credentials/local/local_credentials.cc
  src/core/lib/security/credentials/oauth2/oauth2_credentials.cc
  src/core/lib/security/credentials/plugin/plugin_credentials.cc
  src/core/lib/security/credentials/ssl/ssl_credentials.cc
  src/core/lib/security/security_connector/alts_security_connector.cc
  src/core/lib/security/security_connector/local_security_connector.cc
  src/core/lib/security/security_connector/security_connector.cc
  src/core/lib/security/transport/client_auth_filter.cc
  src/core/lib/security/transport/secure_endpoint.cc
  src/core/lib/security/transport/security_handshaker.cc
  src/core/lib/security/transport/server_auth_filter.cc
  src/core/lib/security/transport/target_authority_table.cc
  src/core/lib/security/transport/tsi_error.cc
  src/core/lib/security/util/json_util.cc
  src/core/lib/surface/init_secure.cc
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
  src/core/tsi/alts/handshaker/alts_tsi_event.cc
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc
  src/core/lib/security/credentials/alts/check_gcp_environment.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc
  src/core/tsi/alts/handshaker/alts_handshaker_service_api.cc
  src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.cc
  src/core/tsi/alts/handshaker/alts_tsi_utils.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/alts/handshaker/altscontext.pb.c
  src/core/tsi/alts/handshaker/handshaker.pb.c
  src/core/tsi/alts/handshaker/transport_security_common.pb.c
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
  src/core/tsi/transport_security.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/tsi/alts_transport_security.cc
  src/core/tsi/fake_transport_security.cc
  src/core/tsi/local_transport_security.cc
  src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc
  src/core/tsi/ssl/session_cache/ssl_session_cache.cc
  src/core/tsi/ssl/session_cache/ssl_session_openssl.cc
  src/core/tsi/ssl_transport_security.cc
  src/core/tsi/transport_security_grpc.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc
  src/core/ext/transport/inproc/inproc_plugin.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/google/protobuf/duration.pb.c
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/google/protobuf/timestamp.pb.c
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc
  src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc
  src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc
  src/cpp/ext/filters/census/grpc_context.cc
  src/core/ext/filters/max_age/max_age_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc
  src/core/ext/filters/workarounds/workaround_utils.cc
  src/core/plugin_registry/grpc_plugin_registry.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc PROPERTIES COMPILE_PDB_NAME "grpc"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  ${_gRPC_CARES_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/grpc_security.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc_cronet
  src/core/lib/surface/init.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/transport/cronet/client/secure/cronet_channel_create.cc
  src/core/ext/transport/cronet/transport/cronet_api_dummy.cc
  src/core/ext/transport/cronet/transport/cronet_transport.cc
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/lib/http/httpcli_security_connector.cc
  src/core/lib/security/context/security_context.cc
  src/core/lib/security/credentials/alts/alts_credentials.cc
  src/core/lib/security/credentials/composite/composite_credentials.cc
  src/core/lib/security/credentials/credentials.cc
  src/core/lib/security/credentials/credentials_metadata.cc
  src/core/lib/security/credentials/fake/fake_credentials.cc
  src/core/lib/security/credentials/google_default/credentials_generic.cc
  src/core/lib/security/credentials/google_default/google_default_credentials.cc
  src/core/lib/security/credentials/iam/iam_credentials.cc
  src/core/lib/security/credentials/jwt/json_token.cc
  src/core/lib/security/credentials/jwt/jwt_credentials.cc
  src/core/lib/security/credentials/jwt/jwt_verifier.cc
  src/core/lib/security/credentials/local/local_credentials.cc
  src/core/lib/security/credentials/oauth2/oauth2_credentials.cc
  src/core/lib/security/credentials/plugin/plugin_credentials.cc
  src/core/lib/security/credentials/ssl/ssl_credentials.cc
  src/core/lib/security/security_connector/alts_security_connector.cc
  src/core/lib/security/security_connector/local_security_connector.cc
  src/core/lib/security/security_connector/security_connector.cc
  src/core/lib/security/transport/client_auth_filter.cc
  src/core/lib/security/transport/secure_endpoint.cc
  src/core/lib/security/transport/security_handshaker.cc
  src/core/lib/security/transport/server_auth_filter.cc
  src/core/lib/security/transport/target_authority_table.cc
  src/core/lib/security/transport/tsi_error.cc
  src/core/lib/security/util/json_util.cc
  src/core/lib/surface/init_secure.cc
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
  src/core/tsi/alts/handshaker/alts_tsi_event.cc
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc
  src/core/lib/security/credentials/alts/check_gcp_environment.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc
  src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc
  src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc
  src/core/tsi/alts/handshaker/alts_handshaker_service_api.cc
  src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.cc
  src/core/tsi/alts/handshaker/alts_tsi_utils.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/alts/handshaker/altscontext.pb.c
  src/core/tsi/alts/handshaker/handshaker.pb.c
  src/core/tsi/alts/handshaker/transport_security_common.pb.c
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
  src/core/tsi/transport_security.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/tsi/alts_transport_security.cc
  src/core/tsi/fake_transport_security.cc
  src/core/tsi/local_transport_security.cc
  src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc
  src/core/tsi/ssl/session_cache/ssl_session_cache.cc
  src/core/tsi/ssl/session_cache/ssl_session_openssl.cc
  src/core/tsi/ssl_transport_security.cc
  src/core/tsi/transport_security_grpc.cc
  src/core/plugin_registry/grpc_cronet_plugin_registry.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_cronet PROPERTIES COMPILE_PDB_NAME "grpc_cronet"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_cronet.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_cronet
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_cronet
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  ${_gRPC_CARES_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/grpc_cronet.h
  include/grpc/grpc_security.h
  include/grpc/grpc_security_constants.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc_cronet EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(grpc_test_util
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/core/security/oauth2_utils.cc
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  test/core/end2end/cq_verifier.cc
  test/core/end2end/fixtures/http_proxy_fixture.cc
  test/core/end2end/fixtures/proxy.cc
  test/core/iomgr/endpoint_tests.cc
  test/core/util/debugger_macros.cc
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
  test/core/util/slice_splitter.cc
  test/core/util/subprocess_posix.cc
  test/core/util/subprocess_windows.cc
  test/core/util/tracer_util.cc
  test/core/util/trickle_endpoint.cc
  test/core/util/cmdline.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util PROPERTIES COMPILE_PDB_NAME "grpc_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_test_util
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
  grpc
)

foreach(_hdr
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(grpc_test_util_unsecure
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  test/core/end2end/cq_verifier.cc
  test/core/end2end/fixtures/http_proxy_fixture.cc
  test/core/end2end/fixtures/proxy.cc
  test/core/iomgr/endpoint_tests.cc
  test/core/util/debugger_macros.cc
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
  test/core/util/slice_splitter.cc
  test/core/util/subprocess_posix.cc
  test/core/util/subprocess_windows.cc
  test/core/util/tracer_util.cc
  test/core/util/trickle_endpoint.cc
  test/core/util/cmdline.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_test_util_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_test_util_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_test_util_unsecure
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
  gpr_test_util
  grpc_unsecure
)

foreach(_hdr
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

endif (gRPC_BUILD_TESTS)

add_library(grpc_unsecure
  src/core/lib/surface/init.cc
  src/core/lib/surface/init_unsecure.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/transport/inproc/inproc_plugin.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc
  src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc
  src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/google/protobuf/duration.pb.c
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/google/protobuf/timestamp.pb.c
  src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
  src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc
  src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc
  src/cpp/ext/filters/census/grpc_context.cc
  src/core/ext/filters/max_age/max_age_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc
  src/core/ext/filters/workarounds/workaround_utils.cc
  src/core/plugin_registry/grpc_unsecure_plugin_registry.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  ${_gRPC_CARES_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(reconnect_server
  test/core/util/reconnect_server.cc
)

if(WIN32 AND MSVC)
  set_target_properties(reconnect_server PROPERTIES COMPILE_PDB_NAME "reconnect_server"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/reconnect_server.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(reconnect_server
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(reconnect_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  test_tcp_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(test_tcp_server
  test/core/util/test_tcp_server.cc
)

if(WIN32 AND MSVC)
  set_target_properties(test_tcp_server PROPERTIES COMPILE_PDB_NAME "test_tcp_server"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/test_tcp_server.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(test_tcp_server
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(test_tcp_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)

add_library(grpc++
  src/cpp/client/insecure_credentials.cc
  src/cpp/client/secure_credentials.cc
  src/cpp/common/auth_property_iterator.cc
  src/cpp/common/secure_auth_context.cc
  src/cpp/common/secure_channel_arguments.cc
  src/cpp/common/secure_create_auth_context.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/server/secure_server_credentials.cc
  src/cpp/client/channel_cc.cc
  src/cpp/client/client_context.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/credentials_cc.cc
  src/cpp/client/generic_stub.cc
  src/cpp/common/alarm.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health.pb.c
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++ PROPERTIES COMPILE_PDB_NAME "grpc++"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc++
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
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
  include/grpc++/impl/codegen/core_codegen.h
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
  include/grpcpp/impl/codegen/core_codegen.h
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
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/config.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
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
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
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
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc++ EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc++_core_stats
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/core/stats.grpc.pb.h
  src/cpp/util/core_stats.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_core_stats PROPERTIES COMPILE_PDB_NAME "grpc++_core_stats"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_core_stats.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/core/stats.proto
)

target_include_directories(grpc++_core_stats
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_core_stats
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)

add_library(grpc++_cronet
  src/cpp/client/cronet_credentials.cc
  src/cpp/client/insecure_credentials.cc
  src/cpp/common/insecure_create_auth_context.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/client/channel_cc.cc
  src/cpp/client/client_context.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/credentials_cc.cc
  src/cpp/client/generic_stub.cc
  src/cpp/common/alarm.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health.pb.c
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc
  src/core/ext/transport/chttp2/client/authority.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/chttp2_plugin.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
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
  src/core/lib/channel/handshaker_factory.cc
  src/core/lib/channel/handshaker_registry.cc
  src/core/lib/channel/status_util.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/compression/stream_compression.cc
  src/core/lib/compression/stream_compression_gzip.cc
  src/core/lib/compression/stream_compression_identity.cc
  src/core/lib/debug/stats.cc
  src/core/lib/debug/stats_data.cc
  src/core/lib/http/format_request.cc
  src/core/lib/http/httpcli.cc
  src/core/lib/http/parser.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_uv.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_epollex_linux.cc
  src/core/lib/iomgr/ev_epollsig_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/ev_windows.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/executor.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/gethostname_fallback.cc
  src/core/lib/iomgr/gethostname_host_name_max.cc
  src/core/lib/iomgr/gethostname_sysconf.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_custom.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_uv.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/is_epollexclusive_available.cc
  src/core/lib/iomgr/load_file.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/network_status_tracker.cc
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
  src/core/lib/iomgr/sockaddr_utils.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_uv.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/tcp_client.cc
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
  src/core/lib/iomgr/wakeup_fd_cv.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/json/json.cc
  src/core/lib/json/json_reader.cc
  src/core/lib/json/json_string.cc
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
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/server.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/byte_stream.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/metadata.cc
  src/core/lib/transport/metadata_batch.cc
  src/core/lib/transport/pid_controller.cc
  src/core/lib/transport/service_config.cc
  src/core/lib/transport/static_metadata.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/status_metadata.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/lib/debug/trace.cc
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/message_compress_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/client_channel/backup_poller.cc
  src/core/ext/filters/client_channel/channel_connectivity.cc
  src/core/ext/filters/client_channel/client_channel.cc
  src/core/ext/filters/client_channel/client_channel_channelz.cc
  src/core/ext/filters/client_channel/client_channel_factory.cc
  src/core/ext/filters/client_channel/client_channel_plugin.cc
  src/core/ext/filters/client_channel/connector.cc
  src/core/ext/filters/client_channel/http_connect_handshaker.cc
  src/core/ext/filters/client_channel/http_proxy.cc
  src/core/ext/filters/client_channel/lb_policy.cc
  src/core/ext/filters/client_channel/lb_policy_factory.cc
  src/core/ext/filters/client_channel/lb_policy_registry.cc
  src/core/ext/filters/client_channel/method_params.cc
  src/core/ext/filters/client_channel/parse_address.cc
  src/core/ext/filters/client_channel/proxy_mapper.cc
  src/core/ext/filters/client_channel/proxy_mapper_registry.cc
  src/core/ext/filters/client_channel/resolver.cc
  src/core/ext/filters/client_channel/resolver_registry.cc
  src/core/ext/filters/client_channel/retry_throttle.cc
  src/core/ext/filters/client_channel/subchannel.cc
  src/core/ext/filters/client_channel/subchannel_index.cc
  src/core/ext/filters/client_channel/uri_parser.cc
  src/core/ext/filters/deadline/deadline_filter.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/cpp/ext/filters/census/grpc_context.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_cronet PROPERTIES COMPILE_PDB_NAME "grpc++_cronet"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_cronet.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc++_cronet
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_cronet
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
  grpc_cronet
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
  include/grpc++/impl/codegen/core_codegen.h
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
  include/grpcpp/impl/codegen/core_codegen.h
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
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/config.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
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
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
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
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc++_cronet EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


if (gRPC_BUILD_CODEGEN)
add_library(grpc++_error_details
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/status/status.grpc.pb.h
  src/cpp/util/error_details.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_error_details PROPERTIES COMPILE_PDB_NAME "grpc++_error_details"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_error_details.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/status/status.proto
)

target_include_directories(grpc++_error_details
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_error_details
  ${_gRPC_BASELIB_LIBRARIES}
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
endif (gRPC_BUILD_CODEGEN)


if (gRPC_INSTALL)
  install(TARGETS grpc++_error_details EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc++_proto_reflection_desc_db
  test/cpp/util/proto_reflection_descriptor_database.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_proto_reflection_desc_db PROPERTIES COMPILE_PDB_NAME "grpc++_proto_reflection_desc_db"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_proto_reflection_desc_db.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc++_proto_reflection_desc_db
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_proto_reflection_desc_db
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
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
endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc++_reflection
  src/cpp/ext/proto_server_reflection.cc
  src/cpp/ext/proto_server_reflection_plugin.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_reflection PROPERTIES COMPILE_PDB_NAME "grpc++_reflection"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_reflection.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc++_reflection
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_reflection
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
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
endif (gRPC_BUILD_CODEGEN)


if (gRPC_INSTALL)
  install(TARGETS grpc++_reflection EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(grpc++_test_config
  test/cpp/util/test_config_cc.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_config PROPERTIES COMPILE_PDB_NAME "grpc++_test_config"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_config.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc++_test_config
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_test_config
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc++_test_util
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_mock.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  test/cpp/end2end/test_service_impl.cc
  test/cpp/util/byte_buffer_proto_helper.cc
  test/cpp/util/channel_trace_proto_helper.cc
  test/cpp/util/create_test_channel.cc
  test/cpp/util/string_ref_helper.cc
  test/cpp/util/subprocess.cc
  test/cpp/util/test_credentials_provider.cc
  src/cpp/codegen/codegen_init.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_util PROPERTIES COMPILE_PDB_NAME "grpc++_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/health/v1/health.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/duplicate/echo_duplicate.proto
)

target_include_directories(grpc++_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_test_util
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
  grpc
)

foreach(_hdr
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
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
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
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc++_test_util_unsecure
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/health/v1/health.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_mock.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  test/cpp/end2end/test_service_impl.cc
  test/cpp/util/byte_buffer_proto_helper.cc
  test/cpp/util/string_ref_helper.cc
  test/cpp/util/subprocess.cc
  src/cpp/codegen/codegen_init.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_util_unsecure PROPERTIES COMPILE_PDB_NAME "grpc++_test_util_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_util_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/health/v1/health.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/duplicate/echo_duplicate.proto
)

target_include_directories(grpc++_test_util_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_test_util_unsecure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_unsecure
  grpc_test_util_unsecure
  grpc_unsecure
)

foreach(_hdr
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
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
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
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)

add_library(grpc++_unsecure
  src/cpp/client/insecure_credentials.cc
  src/cpp/common/insecure_create_auth_context.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/client/channel_cc.cc
  src/cpp/client/client_context.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/credentials_cc.cc
  src/cpp/client/generic_stub.cc
  src/cpp/common/alarm.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health.pb.c
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_unsecure PROPERTIES COMPILE_PDB_NAME "grpc++_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc++_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
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
  include/grpc++/impl/codegen/core_codegen.h
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
  include/grpcpp/impl/codegen/core_codegen.h
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
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/config.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
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
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/load_reporting.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
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
  include/grpc++/impl/codegen/core_codegen_interface.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/grpc_library.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
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
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_hook.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/core_codegen_interface.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/grpc_library.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc++_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(grpc_benchmark
  test/cpp/microbenchmarks/helpers.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_benchmark PROPERTIES COMPILE_PDB_NAME "grpc_benchmark"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_benchmark.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_benchmark
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_benchmark
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_unsecure
  grpc_test_util_unsecure
  grpc_unsecure
  ${_gRPC_GFLAGS_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(grpc_cli_libs
  test/cpp/util/cli_call.cc
  test/cpp/util/cli_credentials.cc
  test/cpp/util/grpc_tool.cc
  test/cpp/util/proto_file_parser.cc
  test/cpp/util/service_describer.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_cli_libs PROPERTIES COMPILE_PDB_NAME "grpc_cli_libs"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_cli_libs.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc_cli_libs
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cli_libs
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_proto_reflection_desc_db
  grpc++
  grpc
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
endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)

add_library(grpc_plugin_support
  src/compiler/cpp_generator.cc
  src/compiler/csharp_generator.cc
  src/compiler/node_generator.cc
  src/compiler/objective_c_generator.cc
  src/compiler/php_generator.cc
  src/compiler/python_generator.cc
  src/compiler/ruby_generator.cc
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_plugin_support PROPERTIES COMPILE_PDB_NAME "grpc_plugin_support"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_plugin_support.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_plugin_support
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
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


if (gRPC_INSTALL)
  install(TARGETS grpc_plugin_support EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


if (gRPC_BUILD_CODEGEN)
add_library(grpcpp_channelz
  src/cpp/server/channelz/channelz_service.cc
  src/cpp/server/channelz/channelz_service_plugin.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
)

if(WIN32 AND MSVC)
  set_target_properties(grpcpp_channelz PROPERTIES COMPILE_PDB_NAME "grpcpp_channelz"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpcpp_channelz.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)

target_include_directories(grpcpp_channelz
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpcpp_channelz
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
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
endif (gRPC_BUILD_CODEGEN)


if (gRPC_INSTALL)
  install(TARGETS grpcpp_channelz EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(http2_client_main
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
)

if(WIN32 AND MSVC)
  set_target_properties(http2_client_main PROPERTIES COMPILE_PDB_NAME "http2_client_main"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/http2_client_main.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(http2_client_main
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http2_client_main
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  grpc++_test_config
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(interop_client_helper
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  test/cpp/interop/client_helper.cc
)

if(WIN32 AND MSVC)
  set_target_properties(interop_client_helper PROPERTIES COMPILE_PDB_NAME "interop_client_helper"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/interop_client_helper.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)

target_include_directories(interop_client_helper
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client_helper
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(interop_client_main
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
  test/cpp/interop/client.cc
  test/cpp/interop/interop_client.cc
)

if(WIN32 AND MSVC)
  set_target_properties(interop_client_main PROPERTIES COMPILE_PDB_NAME "interop_client_main"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/interop_client_main.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(interop_client_main
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client_main
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  interop_client_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(interop_server_helper
  test/cpp/interop/server_helper.cc
)

if(WIN32 AND MSVC)
  set_target_properties(interop_server_helper PROPERTIES COMPILE_PDB_NAME "interop_server_helper"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/interop_server_helper.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(interop_server_helper
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server_helper
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(interop_server_lib
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
)

if(WIN32 AND MSVC)
  set_target_properties(interop_server_lib PROPERTIES COMPILE_PDB_NAME "interop_server_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/interop_server_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(interop_server_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server_lib
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  interop_server_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(interop_server_main
  test/cpp/interop/interop_server_bootstrap.cc
)

if(WIN32 AND MSVC)
  set_target_properties(interop_server_main PROPERTIES COMPILE_PDB_NAME "interop_server_main"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/interop_server_main.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(interop_server_main
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server_main
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  interop_server_lib
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

if (gRPC_BUILD_CODEGEN)
add_library(qps
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
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  test/cpp/qps/benchmark_config.cc
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/driver.cc
  test/cpp/qps/parse_json.cc
  test/cpp/qps/qps_server_builder.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/report.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
)

if(WIN32 AND MSVC)
  set_target_properties(qps PROPERTIES COMPILE_PDB_NAME "qps"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qps.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/payloads.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/stats.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/control.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/benchmark_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/report_qps_scenario_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/worker_service.proto
)

target_include_directories(qps
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++_test_util
  grpc++_core_stats
  grpc++
  grpc
)

endif (gRPC_BUILD_CODEGEN)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_CSHARP_EXT)

add_library(grpc_csharp_ext SHARED
  src/csharp/ext/grpc_csharp_ext.c
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_csharp_ext PROPERTIES COMPILE_PDB_NAME "grpc_csharp_ext"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_csharp_ext.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(grpc_csharp_ext
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_csharp_ext
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)



if (gRPC_INSTALL)
  install(TARGETS grpc_csharp_ext EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CSHARP_EXT)
if (gRPC_BUILD_TESTS)

add_library(bad_client_test
  test/core/bad_client/bad_client.cc
)

if(WIN32 AND MSVC)
  set_target_properties(bad_client_test PROPERTIES COMPILE_PDB_NAME "bad_client_test"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bad_client_test.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(bad_client_test
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bad_client_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(bad_ssl_test_server
  test/core/bad_ssl/server_common.cc
)

if(WIN32 AND MSVC)
  set_target_properties(bad_ssl_test_server PROPERTIES COMPILE_PDB_NAME "bad_ssl_test_server"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bad_ssl_test_server.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(bad_ssl_test_server
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bad_ssl_test_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(end2end_tests
  test/core/end2end/end2end_tests.cc
  test/core/end2end/end2end_test_utils.cc
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
  test/core/end2end/tests/compressed_payload.cc
  test/core/end2end/tests/connectivity.cc
  test/core/end2end/tests/default_host.cc
  test/core/end2end/tests/disappearing_server.cc
  test/core/end2end/tests/empty_batch.cc
  test/core/end2end/tests/filter_call_init_fails.cc
  test/core/end2end/tests/filter_causes_close.cc
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
  test/core/end2end/tests/network_status_change.cc
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
  test/core/end2end/tests/retry_cancellation.cc
  test/core/end2end/tests/retry_disabled.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_initial_batch.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_subsequent_batch.cc
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
)

if(WIN32 AND MSVC)
  set_target_properties(end2end_tests PROPERTIES COMPILE_PDB_NAME "end2end_tests"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/end2end_tests.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(end2end_tests
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(end2end_tests
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(end2end_nosec_tests
  test/core/end2end/end2end_nosec_tests.cc
  test/core/end2end/end2end_test_utils.cc
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
  test/core/end2end/tests/compressed_payload.cc
  test/core/end2end/tests/connectivity.cc
  test/core/end2end/tests/default_host.cc
  test/core/end2end/tests/disappearing_server.cc
  test/core/end2end/tests/empty_batch.cc
  test/core/end2end/tests/filter_call_init_fails.cc
  test/core/end2end/tests/filter_causes_close.cc
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
  test/core/end2end/tests/network_status_change.cc
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
  test/core/end2end/tests/retry_cancellation.cc
  test/core/end2end/tests/retry_disabled.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_initial_batch.cc
  test/core/end2end/tests/retry_exceeds_buffer_size_in_subsequent_batch.cc
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
)

if(WIN32 AND MSVC)
  set_target_properties(end2end_nosec_tests PROPERTIES COMPILE_PDB_NAME "end2end_nosec_tests"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if (gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/end2end_nosec_tests.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()


target_include_directories(end2end_nosec_tests
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(end2end_nosec_tests
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)

if (gRPC_BUILD_TESTS)

add_executable(algorithm_test
  test/core/compression/algorithm_test.cc
)


target_include_directories(algorithm_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(algorithm_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alloc_test
  test/core/gpr/alloc_test.cc
)


target_include_directories(alloc_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(alloc_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alpn_test
  test/core/transport/chttp2/alpn_test.cc
)


target_include_directories(alpn_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(alpn_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(arena_test
  test/core/gpr/arena_test.cc
)


target_include_directories(arena_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(arena_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(avl_test
  test/core/avl/avl_test.cc
)


target_include_directories(avl_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(avl_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
  grpc
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bad_server_response_test
  test/core/end2end/bad_server_response_test.cc
)


target_include_directories(bad_server_response_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bad_server_response_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  test_tcp_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bin_decoder_test
  test/core/transport/chttp2/bin_decoder_test.cc
)


target_include_directories(bin_decoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bin_decoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bin_encoder_test
  test/core/transport/chttp2/bin_encoder_test.cc
)


target_include_directories(bin_encoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bin_encoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_create_test
  test/core/surface/channel_create_test.cc
)


target_include_directories(channel_create_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(channel_create_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

add_executable(check_epollexclusive
  test/build/check_epollexclusive.c
)


target_include_directories(check_epollexclusive
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(check_epollexclusive
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)

if (gRPC_BUILD_TESTS)

add_executable(chttp2_hpack_encoder_test
  test/core/transport/chttp2/hpack_encoder_test.cc
)


target_include_directories(chttp2_hpack_encoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(chttp2_hpack_encoder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_stream_map_test
  test/core/transport/chttp2/stream_map_test.cc
)


target_include_directories(chttp2_stream_map_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(chttp2_stream_map_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_varint_test
  test/core/transport/chttp2/varint_test.cc
)


target_include_directories(chttp2_varint_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(chttp2_varint_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cmdline_test
  test/core/util/cmdline_test.cc
)


target_include_directories(cmdline_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(cmdline_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
  gpr_test_util
  grpc_test_util
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(combiner_test
  test/core/iomgr/combiner_test.cc
)


target_include_directories(combiner_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(combiner_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(compression_test
  test/core/compression/compression_test.cc
)


target_include_directories(compression_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(compression_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(concurrent_connectivity_test
  test/core/surface/concurrent_connectivity_test.cc
)


target_include_directories(concurrent_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(concurrent_connectivity_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(connection_refused_test
  test/core/end2end/connection_refused_test.cc
)


target_include_directories(connection_refused_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(connection_refused_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dns_resolver_connectivity_test
  test/core/client_channel/resolvers/dns_resolver_connectivity_test.cc
)


target_include_directories(dns_resolver_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_connectivity_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dns_resolver_cooldown_test
  test/core/client_channel/resolvers/dns_resolver_cooldown_test.cc
)


target_include_directories(dns_resolver_cooldown_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_cooldown_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dns_resolver_test
  test/core/client_channel/resolvers/dns_resolver_test.cc
)


target_include_directories(dns_resolver_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(dns_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(dualstack_socket_test
  test/core/end2end/dualstack_socket_test.cc
)


target_include_directories(dualstack_socket_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(dualstack_socket_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(endpoint_pair_test
  test/core/iomgr/endpoint_pair_test.cc
)


target_include_directories(endpoint_pair_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(endpoint_pair_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(error_test
  test/core/iomgr/error_test.cc
)


target_include_directories(error_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(error_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(ev_epollex_linux_test
  test/core/iomgr/ev_epollex_linux_test.cc
)


target_include_directories(ev_epollex_linux_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(ev_epollex_linux_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(ev_epollsig_linux_test
  test/core/iomgr/ev_epollsig_linux_test.cc
)


target_include_directories(ev_epollsig_linux_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(ev_epollsig_linux_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fake_resolver_test
  test/core/client_channel/resolvers/fake_resolver_test.cc
)


target_include_directories(fake_resolver_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fake_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(fake_transport_security_test
  test/core/tsi/fake_transport_security_test.cc
  test/core/tsi/transport_security_test_lib.cc
)


target_include_directories(fake_transport_security_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fake_transport_security_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
  grpc
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(fd_conservation_posix_test
  test/core/iomgr/fd_conservation_posix_test.cc
)


target_include_directories(fd_conservation_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fd_conservation_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(fd_posix_test
  test/core/iomgr/fd_posix_test.cc
)


target_include_directories(fd_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fd_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_client
  test/core/fling/client.cc
)


target_include_directories(fling_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fling_client
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_server
  test/core/fling/server.cc
)


target_include_directories(fling_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fling_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(fling_stream_test
  test/core/fling/fling_stream_test.cc
)


target_include_directories(fling_stream_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fling_stream_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(fling_test
  test/core/fling/fling_test.cc
)


target_include_directories(fling_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fling_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC)

add_executable(fork_test
  test/core/gprpp/fork_test.cc
)


target_include_directories(fork_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(fork_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(goaway_server_test
  test/core/end2end/goaway_server_test.cc
)


target_include_directories(goaway_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(goaway_server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_cpu_test
  test/core/gpr/cpu_test.cc
)


target_include_directories(gpr_cpu_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_cpu_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_env_test
  test/core/gpr/env_test.cc
)


target_include_directories(gpr_env_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_env_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_host_port_test
  test/core/gpr/host_port_test.cc
)


target_include_directories(gpr_host_port_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_host_port_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_log_test
  test/core/gpr/log_test.cc
)


target_include_directories(gpr_log_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_log_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_manual_constructor_test
  test/core/gprpp/manual_constructor_test.cc
)


target_include_directories(gpr_manual_constructor_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_manual_constructor_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_mpscq_test
  test/core/gpr/mpscq_test.cc
)


target_include_directories(gpr_mpscq_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_mpscq_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_spinlock_test
  test/core/gpr/spinlock_test.cc
)


target_include_directories(gpr_spinlock_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_spinlock_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_string_test
  test/core/gpr/string_test.cc
)


target_include_directories(gpr_string_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_string_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_sync_test
  test/core/gpr/sync_test.cc
)


target_include_directories(gpr_sync_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_sync_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_thd_test
  test/core/gprpp/thd_test.cc
)


target_include_directories(gpr_thd_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_thd_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_time_test
  test/core/gpr/time_test.cc
)


target_include_directories(gpr_time_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_time_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_tls_test
  test/core/gpr/tls_test.cc
)


target_include_directories(gpr_tls_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_tls_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_useful_test
  test/core/gpr/useful_test.cc
)


target_include_directories(gpr_useful_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gpr_useful_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_auth_context_test
  test/core/security/auth_context_test.cc
)


target_include_directories(grpc_auth_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_auth_context_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_b64_test
  test/core/slice/b64_test.cc
)


target_include_directories(grpc_b64_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_b64_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_byte_buffer_reader_test
  test/core/surface/byte_buffer_reader_test.cc
)


target_include_directories(grpc_byte_buffer_reader_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_byte_buffer_reader_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_channel_args_test
  test/core/channel/channel_args_test.cc
)


target_include_directories(grpc_channel_args_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_channel_args_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_channel_stack_builder_test
  test/core/channel/channel_stack_builder_test.cc
)


target_include_directories(grpc_channel_stack_builder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_channel_stack_builder_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_channel_stack_test
  test/core/channel/channel_stack_test.cc
)


target_include_directories(grpc_channel_stack_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_channel_stack_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_completion_queue_test
  test/core/surface/completion_queue_test.cc
)


target_include_directories(grpc_completion_queue_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_completion_queue_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_completion_queue_threading_test
  test/core/surface/completion_queue_threading_test.cc
)


target_include_directories(grpc_completion_queue_threading_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_completion_queue_threading_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

add_executable(grpc_create_jwt
  test/core/security/create_jwt.cc
  test/core/util/cmdline.cc
)


target_include_directories(grpc_create_jwt
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_create_jwt
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)

if (gRPC_BUILD_TESTS)

add_executable(grpc_credentials_test
  test/core/security/credentials_test.cc
)


target_include_directories(grpc_credentials_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_credentials_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_fetch_oauth2
  test/core/security/fetch_oauth2.cc
)


target_include_directories(grpc_fetch_oauth2
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_fetch_oauth2
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_ipv6_loopback_available_test
  test/core/iomgr/grpc_ipv6_loopback_available_test.cc
)


target_include_directories(grpc_ipv6_loopback_available_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_ipv6_loopback_available_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(grpc_json_token_test
  test/core/security/json_token_test.cc
)


target_include_directories(grpc_json_token_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_json_token_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_jwt_verifier_test
  test/core/security/jwt_verifier_test.cc
)


target_include_directories(grpc_jwt_verifier_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_jwt_verifier_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

add_executable(grpc_print_google_default_creds_token
  test/core/security/print_google_default_creds_token.cc
  test/core/util/cmdline.cc
)


target_include_directories(grpc_print_google_default_creds_token
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_print_google_default_creds_token
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)

if (gRPC_BUILD_TESTS)

add_executable(grpc_security_connector_test
  test/core/security/security_connector_test.cc
)


target_include_directories(grpc_security_connector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_security_connector_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_ssl_credentials_test
  test/core/security/ssl_credentials_test.cc
)


target_include_directories(grpc_ssl_credentials_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_ssl_credentials_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

add_executable(grpc_verify_jwt
  test/core/security/verify_jwt.cc
  test/core/util/cmdline.cc
)


target_include_directories(grpc_verify_jwt
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(grpc_verify_jwt
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)

if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(handshake_client
  test/core/handshake/client_ssl.cc
)


target_include_directories(handshake_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(handshake_client
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(handshake_server
  test/core/handshake/server_ssl.cc
  test/core/handshake/server_ssl_common.cc
)


target_include_directories(handshake_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(handshake_server
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(handshake_server_with_readahead_handshaker
  test/core/handshake/readahead_handshaker_server_ssl.cc
  test/core/handshake/server_ssl_common.cc
)


target_include_directories(handshake_server_with_readahead_handshaker
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(handshake_server_with_readahead_handshaker
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(handshake_verify_peer_options
  test/core/handshake/verify_peer_options.cc
)


target_include_directories(handshake_verify_peer_options
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(handshake_verify_peer_options
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(histogram_test
  test/core/util/histogram_test.cc
)


target_include_directories(histogram_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(histogram_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_parser_test
  test/core/transport/chttp2/hpack_parser_test.cc
)


target_include_directories(hpack_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(hpack_parser_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_table_test
  test/core/transport/chttp2/hpack_table_test.cc
)


target_include_directories(hpack_table_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(hpack_table_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_parser_test
  test/core/http/parser_test.cc
)


target_include_directories(http_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(http_parser_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(httpcli_format_request_test
  test/core/http/format_request_test.cc
)


target_include_directories(httpcli_format_request_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(httpcli_format_request_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(httpcli_test
  test/core/http/httpcli_test.cc
)


target_include_directories(httpcli_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(httpcli_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(httpscli_test
  test/core/http/httpscli_test.cc
)


target_include_directories(httpscli_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(httpscli_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(init_test
  test/core/surface/init_test.cc
)


target_include_directories(init_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(init_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(invalid_call_argument_test
  test/core/end2end/invalid_call_argument_test.cc
)


target_include_directories(invalid_call_argument_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(invalid_call_argument_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_rewrite
  test/core/json/json_rewrite.cc
)


target_include_directories(json_rewrite
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(json_rewrite
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_rewrite_test
  test/core/json/json_rewrite_test.cc
)


target_include_directories(json_rewrite_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(json_rewrite_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_stream_error_test
  test/core/json/json_stream_error_test.cc
)


target_include_directories(json_stream_error_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(json_stream_error_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_test
  test/core/json/json_test.cc
)


target_include_directories(json_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(json_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(lame_client_test
  test/core/surface/lame_client_test.cc
)


target_include_directories(lame_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(lame_client_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(load_file_test
  test/core/iomgr/load_file_test.cc
)


target_include_directories(load_file_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(load_file_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_profile_client
  test/core/memory_usage/client.cc
)


target_include_directories(memory_profile_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(memory_profile_client
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_profile_server
  test/core/memory_usage/server.cc
)


target_include_directories(memory_profile_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(memory_profile_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(memory_profile_test
  test/core/memory_usage/memory_usage_test.cc
)


target_include_directories(memory_profile_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(memory_profile_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(message_compress_test
  test/core/compression/message_compress_test.cc
)


target_include_directories(message_compress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(message_compress_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(minimal_stack_is_minimal_test
  test/core/channel/minimal_stack_is_minimal_test.cc
)


target_include_directories(minimal_stack_is_minimal_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(minimal_stack_is_minimal_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(multiple_server_queues_test
  test/core/end2end/multiple_server_queues_test.cc
)


target_include_directories(multiple_server_queues_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(multiple_server_queues_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(murmur_hash_test
  test/core/gpr/murmur_hash_test.cc
)


target_include_directories(murmur_hash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(murmur_hash_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(no_server_test
  test/core/end2end/no_server_test.cc
)


target_include_directories(no_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(no_server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(num_external_connectivity_watchers_test
  test/core/surface/num_external_connectivity_watchers_test.cc
)


target_include_directories(num_external_connectivity_watchers_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(num_external_connectivity_watchers_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(parse_address_test
  test/core/client_channel/parse_address_test.cc
)


target_include_directories(parse_address_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(parse_address_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_encoding_test
  test/core/slice/percent_encoding_test.cc
)


target_include_directories(percent_encoding_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(percent_encoding_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(pollset_set_test
  test/core/iomgr/pollset_set_test.cc
)


target_include_directories(pollset_set_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(pollset_set_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(resolve_address_posix_test
  test/core/iomgr/resolve_address_posix_test.cc
)


target_include_directories(resolve_address_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(resolve_address_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(resolve_address_test
  test/core/iomgr/resolve_address_test.cc
)


target_include_directories(resolve_address_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(resolve_address_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(resource_quota_test
  test/core/iomgr/resource_quota_test.cc
)


target_include_directories(resource_quota_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(resource_quota_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(secure_channel_create_test
  test/core/surface/secure_channel_create_test.cc
)


target_include_directories(secure_channel_create_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(secure_channel_create_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(secure_endpoint_test
  test/core/security/secure_endpoint_test.cc
)


target_include_directories(secure_endpoint_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(secure_endpoint_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sequential_connectivity_test
  test/core/surface/sequential_connectivity_test.cc
)


target_include_directories(sequential_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(sequential_connectivity_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_chttp2_test
  test/core/surface/server_chttp2_test.cc
)


target_include_directories(server_chttp2_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(server_chttp2_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_test
  test/core/surface/server_test.cc
)


target_include_directories(server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_buffer_test
  test/core/slice/slice_buffer_test.cc
)


target_include_directories(slice_buffer_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(slice_buffer_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_string_helpers_test
  test/core/slice/slice_string_helpers_test.cc
)


target_include_directories(slice_string_helpers_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(slice_string_helpers_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_test
  test/core/slice/slice_test.cc
)


target_include_directories(slice_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(slice_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sockaddr_resolver_test
  test/core/client_channel/resolvers/sockaddr_resolver_test.cc
)


target_include_directories(sockaddr_resolver_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(sockaddr_resolver_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sockaddr_utils_test
  test/core/iomgr/sockaddr_utils_test.cc
)


target_include_directories(sockaddr_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(sockaddr_utils_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(socket_utils_test
  test/core/iomgr/socket_utils_test.cc
)


target_include_directories(socket_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(socket_utils_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(ssl_transport_security_test
  test/core/tsi/ssl_transport_security_test.cc
  test/core/tsi/transport_security_test_lib.cc
)


target_include_directories(ssl_transport_security_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(ssl_transport_security_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr_test_util
  gpr
  grpc
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(status_conversion_test
  test/core/transport/status_conversion_test.cc
)


target_include_directories(status_conversion_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(status_conversion_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(stream_compression_test
  test/core/compression/stream_compression_test.cc
)


target_include_directories(stream_compression_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(stream_compression_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(stream_owned_slice_test
  test/core/transport/stream_owned_slice_test.cc
)


target_include_directories(stream_owned_slice_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(stream_owned_slice_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(tcp_client_posix_test
  test/core/iomgr/tcp_client_posix_test.cc
)


target_include_directories(tcp_client_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(tcp_client_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(tcp_client_uv_test
  test/core/iomgr/tcp_client_uv_test.cc
)


target_include_directories(tcp_client_uv_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(tcp_client_uv_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(tcp_posix_test
  test/core/iomgr/tcp_posix_test.cc
)


target_include_directories(tcp_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(tcp_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(tcp_server_posix_test
  test/core/iomgr/tcp_server_posix_test.cc
)


target_include_directories(tcp_server_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(tcp_server_posix_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(tcp_server_uv_test
  test/core/iomgr/tcp_server_uv_test.cc
)


target_include_directories(tcp_server_uv_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(tcp_server_uv_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(time_averaged_stats_test
  test/core/iomgr/time_averaged_stats_test.cc
)


target_include_directories(time_averaged_stats_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(time_averaged_stats_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timeout_encoding_test
  test/core/transport/timeout_encoding_test.cc
)


target_include_directories(timeout_encoding_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(timeout_encoding_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timer_heap_test
  test/core/iomgr/timer_heap_test.cc
)


target_include_directories(timer_heap_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(timer_heap_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timer_list_test
  test/core/iomgr/timer_list_test.cc
)


target_include_directories(timer_list_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(timer_list_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_connectivity_state_test
  test/core/transport/connectivity_state_test.cc
)


target_include_directories(transport_connectivity_state_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(transport_connectivity_state_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_metadata_test
  test/core/transport/metadata_test.cc
)


target_include_directories(transport_metadata_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(transport_metadata_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(transport_security_test
  test/core/tsi/transport_security_test.cc
)


target_include_directories(transport_security_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(transport_security_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(udp_server_test
  test/core/iomgr/udp_server_test.cc
)


target_include_directories(udp_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(udp_server_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(uri_parser_test
  test/core/client_channel/uri_parser_test.cc
)


target_include_directories(uri_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(uri_parser_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(wakeup_fd_cv_test
  test/core/iomgr/wakeup_fd_cv_test.cc
)


target_include_directories(wakeup_fd_cv_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(wakeup_fd_cv_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alarm_test
  test/cpp/common/alarm_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alarm_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alarm_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_counter_test
  test/core/tsi/alts/frame_protector/alts_counter_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_counter_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_counter_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_crypt_test
  test/core/tsi/alts/crypt/aes_gcm_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_crypt_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_crypt_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_crypter_test
  test/core/tsi/alts/frame_protector/alts_crypter_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_crypter_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_crypter_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_frame_handler_test
  test/core/tsi/alts/frame_protector/frame_handler_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_frame_handler_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_frame_handler_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_frame_protector_test
  test/core/tsi/alts/frame_protector/alts_frame_protector_test.cc
  test/core/tsi/transport_security_test_lib.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_frame_protector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_frame_protector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_grpc_record_protocol_test
  test/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_grpc_record_protocol_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_grpc_record_protocol_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_handshaker_client_test
  test/core/tsi/alts/handshaker/alts_handshaker_client_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_handshaker_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_handshaker_client_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_handshaker_service_api_test
  test/core/tsi/alts/handshaker/alts_handshaker_service_api_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_handshaker_service_api_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_handshaker_service_api_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_iovec_record_protocol_test
  test/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_iovec_record_protocol_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_iovec_record_protocol_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_security_connector_test
  test/core/security/alts_security_connector_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_security_connector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_security_connector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_tsi_handshaker_test
  test/core/tsi/alts/handshaker/alts_tsi_handshaker_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_tsi_handshaker_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_tsi_handshaker_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_tsi_utils_test
  test/core/tsi/alts/handshaker/alts_tsi_utils_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_tsi_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_tsi_utils_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_zero_copy_grpc_protector_test
  test/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(alts_zero_copy_grpc_protector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(alts_zero_copy_grpc_protector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(async_end2end_test
  test/cpp/end2end/async_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(async_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(async_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(auth_property_iterator_test
  test/cpp/common/auth_property_iterator_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(auth_property_iterator_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(auth_property_iterator_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(backoff_test
  test/core/backoff/backoff_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(backoff_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(backoff_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bdp_estimator_test
  test/core/transport/bdp_estimator_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bdp_estimator_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bdp_estimator_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc++
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_arena
  test/cpp/microbenchmarks/bm_arena.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_arena
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_arena
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_call_create
  test/cpp/microbenchmarks/bm_call_create.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_call_create
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_call_create
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_channel
  test/cpp/microbenchmarks/bm_channel.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_channel
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_channel
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_channel_secure
  test/cpp/microbenchmarks/bm_channel_secure.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_channel_secure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_channel_secure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  gpr_test_util
  gpr
  grpc++_test_config
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_chttp2_hpack
  test/cpp/microbenchmarks/bm_chttp2_hpack.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_chttp2_hpack
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_chttp2_hpack
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_chttp2_transport
  test/cpp/microbenchmarks/bm_chttp2_transport.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_chttp2_transport
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_chttp2_transport
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_closure
  test/cpp/microbenchmarks/bm_closure.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_closure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_closure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_cq
  test/cpp/microbenchmarks/bm_cq.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_cq
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_cq
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_cq_multiple_threads
  test/cpp/microbenchmarks/bm_cq_multiple_threads.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_cq_multiple_threads
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_cq_multiple_threads
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_error
  test/cpp/microbenchmarks/bm_error.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_error
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_error
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_fullstack_streaming_ping_pong
  test/cpp/microbenchmarks/bm_fullstack_streaming_ping_pong.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_fullstack_streaming_ping_pong
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_fullstack_streaming_ping_pong
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_fullstack_streaming_pump
  test/cpp/microbenchmarks/bm_fullstack_streaming_pump.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_fullstack_streaming_pump
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_fullstack_streaming_pump
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_fullstack_trickle
  test/cpp/microbenchmarks/bm_fullstack_trickle.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_fullstack_trickle
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_fullstack_trickle
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_fullstack_unary_ping_pong
  test/cpp/microbenchmarks/bm_fullstack_unary_ping_pong.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_fullstack_unary_ping_pong
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_fullstack_unary_ping_pong
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_metadata
  test/cpp/microbenchmarks/bm_metadata.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_metadata
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_metadata
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bm_pollset
  test/cpp/microbenchmarks/bm_pollset.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(bm_pollset
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(bm_pollset
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_benchmark
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(byte_stream_test
  test/core/transport/byte_stream_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(byte_stream_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(byte_stream_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_arguments_test
  test/cpp/common/channel_arguments_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(channel_arguments_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_arguments_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_filter_test
  test/cpp/common/channel_filter_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(channel_filter_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_filter_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_trace_test
  test/core/channel/channel_trace_test.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)

target_include_directories(channel_trace_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channel_trace_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channelz_registry_test
  test/core/channel/channelz_registry_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(channelz_registry_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_registry_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channelz_service_test
  test/cpp/end2end/channelz_service_test.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)

target_include_directories(channelz_service_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_service_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpcpp_channelz
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channelz_test
  test/core/channel/channelz_test.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/channelz/channelz.proto
)

target_include_directories(channelz_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(channelz_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(check_gcp_environment_linux_test
  test/core/security/check_gcp_environment_linux_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(check_gcp_environment_linux_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(check_gcp_environment_linux_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(check_gcp_environment_windows_test
  test/core/security/check_gcp_environment_windows_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(check_gcp_environment_windows_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(check_gcp_environment_windows_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_settings_timeout_test
  test/core/transport/chttp2/settings_timeout_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(chttp2_settings_timeout_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(chttp2_settings_timeout_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cli_call_test
  test/cpp/util/cli_call_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cli_call_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cli_call_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_cli_libs
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(client_channel_stress_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
  test/cpp/client/client_channel_stress_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/lb/v1/load_balancer.proto
)

target_include_directories(client_channel_stress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_channel_stress_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(client_crash_test
  test/cpp/end2end/client_crash_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(client_crash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_crash_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(client_crash_test_server
  test/cpp/end2end/client_crash_test_server.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(client_crash_test_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_crash_test_server
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(client_lb_end2end_test
  test/cpp/end2end/client_lb_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(client_lb_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(client_lb_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(codegen_test_full
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
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  test/cpp/codegen/codegen_test_full.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/control.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/payloads.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/benchmark_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/report_qps_scenario_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/worker_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/stats.proto
)

target_include_directories(codegen_test_full
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(codegen_test_full
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_core_stats
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(codegen_test_minimal
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
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  test/cpp/codegen/codegen_test_minimal.cc
  src/cpp/codegen/codegen_init.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/control.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/payloads.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/benchmark_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/report_qps_scenario_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/worker_service.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/stats.proto
)

target_include_directories(codegen_test_minimal
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(codegen_test_minimal
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_core_stats
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(credentials_test
  test/cpp/client/credentials_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(credentials_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(credentials_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_byte_buffer_test
  test/cpp/util/byte_buffer_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cxx_byte_buffer_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cxx_byte_buffer_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_slice_test
  test/cpp/util/slice_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cxx_slice_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cxx_slice_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_string_ref_test
  test/cpp/util/string_ref_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cxx_string_ref_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cxx_string_ref_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_time_test
  test/cpp/util/time_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cxx_time_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cxx_time_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(end2end_test
  test/cpp/end2end/end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(error_details_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  test/cpp/util/error_details_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)

target_include_directories(error_details_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(error_details_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_error_details
  grpc++
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(exception_test
  test/cpp/end2end/exception_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(exception_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(exception_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(filter_end2end_test
  test/cpp/end2end/filter_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(filter_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(filter_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(generic_end2end_test
  test/cpp/end2end/generic_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(generic_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(generic_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(golden_file_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/compiler_test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/compiler_test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/compiler_test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/compiler_test.grpc.pb.h
  test/cpp/codegen/golden_file_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/compiler_test.proto
)

target_include_directories(golden_file_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(golden_file_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_alts_credentials_options_test
  test/core/security/grpc_alts_credentials_options_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(grpc_alts_credentials_options_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_alts_credentials_options_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_cli
  test/cpp/util/grpc_cli.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(grpc_cli
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cli
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_cli_libs
  grpc++_proto_reflection_desc_db
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_cpp_plugin
  src/compiler/cpp_plugin.cc
)


target_include_directories(grpc_cpp_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cpp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_cpp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_csharp_plugin
  src/compiler/csharp_plugin.cc
)


target_include_directories(grpc_csharp_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_csharp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_csharp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_node_plugin
  src/compiler/node_plugin.cc
)


target_include_directories(grpc_node_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_node_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_node_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_objective_c_plugin
  src/compiler/objective_c_plugin.cc
)


target_include_directories(grpc_objective_c_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_objective_c_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_objective_c_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_php_plugin
  src/compiler/php_plugin.cc
)


target_include_directories(grpc_php_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_php_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_php_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_python_plugin
  src/compiler/python_plugin.cc
)


target_include_directories(grpc_python_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_python_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_python_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_CODEGEN)

add_executable(grpc_ruby_plugin
  src/compiler/ruby_plugin.cc
)


target_include_directories(grpc_ruby_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_ruby_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_ruby_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif (gRPC_BUILD_CODEGEN)
if (gRPC_BUILD_TESTS)

add_executable(grpc_tool_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  test/cpp/util/grpc_tool_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)

target_include_directories(grpc_tool_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_tool_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_cli_libs
  grpc++_proto_reflection_desc_db
  grpc++_reflection
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpclb_api_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
  test/cpp/grpclb/grpclb_api_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/lb/v1/load_balancer.proto
)

target_include_directories(grpclb_api_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpclb_api_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpclb_end2end_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/lb/v1/load_balancer.grpc.pb.h
  test/cpp/end2end/grpclb_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/lb/v1/load_balancer.proto
)

target_include_directories(grpclb_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpclb_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_cert_test
  test/core/end2end/h2_ssl_cert_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(h2_ssl_cert_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(h2_ssl_cert_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_session_reuse_test
  test/core/end2end/h2_ssl_session_reuse_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(h2_ssl_session_reuse_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(h2_ssl_session_reuse_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(health_service_end2end_test
  test/cpp/end2end/health_service_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(health_service_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(health_service_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(http2_client
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(http2_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http2_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  http2_client_main
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hybrid_end2end_test
  test/cpp/end2end/hybrid_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(hybrid_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(hybrid_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(inlined_vector_test
  test/core/gprpp/inlined_vector_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(inlined_vector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(inlined_vector_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(inproc_sync_unary_ping_pong_test
  test/cpp/qps/inproc_sync_unary_ping_pong_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(inproc_sync_unary_ping_pong_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(inproc_sync_unary_ping_pong_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_core_stats
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(interop_client
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(interop_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  interop_client_main
  interop_client_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(interop_server
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(interop_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  interop_server_main
  interop_server_helper
  interop_server_lib
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(interop_test
  test/cpp/interop/interop_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(interop_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(json_run_localhost
  test/cpp/qps/json_run_localhost.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(json_run_localhost
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(json_run_localhost
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_test
  test/core/gprpp/memory_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(memory_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(memory_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(metrics_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.grpc.pb.h
  test/cpp/interop/metrics_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/metrics.proto
)

target_include_directories(metrics_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(metrics_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(mock_test
  test/cpp/end2end/mock_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(mock_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(mock_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(nonblocking_test
  test/cpp/end2end/nonblocking_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(nonblocking_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(nonblocking_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(noop-benchmark
  test/cpp/microbenchmarks/noop-benchmark.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(noop-benchmark
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(noop-benchmark
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_BENCHMARK_LIBRARIES}
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(orphanable_test
  test/core/gprpp/orphanable_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(orphanable_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(orphanable_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(proto_server_reflection_test
  test/cpp/end2end/proto_server_reflection_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(proto_server_reflection_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(proto_server_reflection_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_proto_reflection_desc_db
  grpc++_reflection
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(proto_utils_test
  test/cpp/codegen/proto_utils_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(proto_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(proto_utils_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(qps_interarrival_test
  test/cpp/qps/qps_interarrival_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(qps_interarrival_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_interarrival_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(qps_json_driver
  test/cpp/qps/qps_json_driver.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(qps_json_driver
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_json_driver
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_core_stats
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(qps_openloop_test
  test/cpp/qps/qps_openloop_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(qps_openloop_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_openloop_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_core_stats
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(qps_worker
  test/cpp/qps/worker.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(qps_worker
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_worker
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_core_stats
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(raw_end2end_test
  test/cpp/end2end/raw_end2end_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(raw_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(raw_end2end_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(reconnect_interop_client
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
  test/cpp/interop/reconnect_interop_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(reconnect_interop_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(reconnect_interop_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(reconnect_interop_server
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
  test/cpp/interop/reconnect_interop_server.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(reconnect_interop_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(reconnect_interop_server
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  reconnect_server
  test_tcp_server
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(ref_counted_ptr_test
  test/core/gprpp/ref_counted_ptr_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(ref_counted_ptr_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(ref_counted_ptr_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(ref_counted_test
  test/core/gprpp/ref_counted_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(ref_counted_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(ref_counted_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(retry_throttle_test
  test/core/client_channel/retry_throttle_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(retry_throttle_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(retry_throttle_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(secure_auth_context_test
  test/cpp/common/secure_auth_context_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(secure_auth_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(secure_auth_context_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(secure_sync_unary_ping_pong_test
  test/cpp/qps/secure_sync_unary_ping_pong_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(secure_sync_unary_ping_pong_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(secure_sync_unary_ping_pong_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  qps
  grpc++_core_stats
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_builder_plugin_test
  test/cpp/end2end/server_builder_plugin_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(server_builder_plugin_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_builder_plugin_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_builder_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  test/cpp/server/server_builder_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)

target_include_directories(server_builder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_builder_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  gpr_test_util
  grpc++_unsecure
  grpc_unsecure
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_POSIX)

add_executable(server_builder_with_socket_mutator_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  test/cpp/server/server_builder_with_socket_mutator_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)

target_include_directories(server_builder_with_socket_mutator_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_builder_with_socket_mutator_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  gpr_test_util
  grpc++_unsecure
  grpc_unsecure
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_context_test_spouse_test
  test/cpp/test/server_context_test_spouse_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(server_context_test_spouse_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_context_test_spouse_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(server_crash_test
  test/cpp/end2end/server_crash_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(server_crash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_crash_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_crash_test_client
  test/cpp/end2end/server_crash_test_client.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(server_crash_test_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_crash_test_client
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_early_return_test
  test/cpp/end2end/server_early_return_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(server_early_return_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_early_return_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_request_call_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  test/cpp/server/server_request_call_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/echo.proto
)

target_include_directories(server_request_call_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(server_request_call_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  gpr_test_util
  grpc++_unsecure
  grpc_unsecure
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(shutdown_test
  test/cpp/end2end/shutdown_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(shutdown_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(shutdown_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_hash_table_test
  test/core/slice/slice_hash_table_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(slice_hash_table_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(slice_hash_table_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_weak_hash_table_test
  test/core/slice/slice_weak_hash_table_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(slice_weak_hash_table_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(slice_weak_hash_table_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(stats_test
  test/core/debug/stats_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(stats_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(stats_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(status_metadata_test
  test/core/transport/status_metadata_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(status_metadata_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(status_metadata_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(status_util_test
  test/core/channel/status_util_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(status_util_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(status_util_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(streaming_throughput_test
  test/cpp/end2end/streaming_throughput_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(streaming_throughput_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(streaming_throughput_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(stress_test
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/metrics.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/cpp/interop/interop_client.cc
  test/cpp/interop/stress_interop_client.cc
  test/cpp/interop/stress_test.cc
  test/cpp/util/metrics_server.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/metrics.proto
)
protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/test.proto
)

target_include_directories(stress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(stress_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(thread_manager_test
  test/cpp/thread_manager/thread_manager_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(thread_manager_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(thread_manager_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_unsecure
  grpc_unsecure
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(thread_stress_test
  test/cpp/end2end/thread_stress_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(thread_stress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(thread_stress_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  grpc++_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_pid_controller_test
  test/core/transport/pid_controller_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(transport_pid_controller_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(transport_pid_controller_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc++
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_security_common_api_test
  test/core/tsi/alts/handshaker/transport_security_common_api_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(transport_security_common_api_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(transport_security_common_api_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  alts_test_util
  gpr
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(writes_per_rpc_test
  test/cpp/performance/writes_per_rpc_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(writes_per_rpc_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(writes_per_rpc_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(public_headers_must_be_c89
  test/core/surface/public_headers_must_be_c89.c
)


target_include_directories(public_headers_must_be_c89
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(public_headers_must_be_c89
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc
  gpr
)

endif (gRPC_BUILD_TESTS)

add_executable(gen_hpack_tables
  tools/codegen/core/gen_hpack_tables.cc
)


target_include_directories(gen_hpack_tables
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gen_hpack_tables
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gpr
  grpc
)


add_executable(gen_legal_metadata_characters
  tools/codegen/core/gen_legal_metadata_characters.cc
)


target_include_directories(gen_legal_metadata_characters
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gen_legal_metadata_characters
  ${_gRPC_ALLTARGETS_LIBRARIES}
)


add_executable(gen_percent_encoding_tables
  tools/codegen/core/gen_percent_encoding_tables.cc
)


target_include_directories(gen_percent_encoding_tables
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(gen_percent_encoding_tables
  ${_gRPC_ALLTARGETS_LIBRARIES}
)

if (gRPC_BUILD_TESTS)

add_executable(badreq_bad_client_test
  test/core/bad_client/tests/badreq.cc
)


target_include_directories(badreq_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(badreq_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(connection_prefix_bad_client_test
  test/core/bad_client/tests/connection_prefix.cc
)


target_include_directories(connection_prefix_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(connection_prefix_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(duplicate_header_bad_client_test
  test/core/bad_client/tests/duplicate_header.cc
)


target_include_directories(duplicate_header_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(duplicate_header_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(head_of_line_blocking_bad_client_test
  test/core/bad_client/tests/head_of_line_blocking.cc
)


target_include_directories(head_of_line_blocking_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(head_of_line_blocking_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(headers_bad_client_test
  test/core/bad_client/tests/headers.cc
)


target_include_directories(headers_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(headers_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(initial_settings_frame_bad_client_test
  test/core/bad_client/tests/initial_settings_frame.cc
)


target_include_directories(initial_settings_frame_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(initial_settings_frame_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(large_metadata_bad_client_test
  test/core/bad_client/tests/large_metadata.cc
)


target_include_directories(large_metadata_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(large_metadata_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_registered_method_bad_client_test
  test/core/bad_client/tests/server_registered_method.cc
)


target_include_directories(server_registered_method_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(server_registered_method_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(simple_request_bad_client_test
  test/core/bad_client/tests/simple_request.cc
)


target_include_directories(simple_request_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(simple_request_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(unknown_frame_bad_client_test
  test/core/bad_client/tests/unknown_frame.cc
)


target_include_directories(unknown_frame_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(unknown_frame_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(window_overflow_bad_client_test
  test/core/bad_client/tests/window_overflow.cc
)


target_include_directories(window_overflow_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(window_overflow_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bad_ssl_cert_server
  test/core/bad_ssl/servers/cert.cc
)


target_include_directories(bad_ssl_cert_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bad_ssl_cert_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  bad_ssl_test_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(bad_ssl_cert_test
  test/core/bad_ssl/bad_ssl_test.cc
)


target_include_directories(bad_ssl_cert_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(bad_ssl_cert_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_census_test
  test/core/end2end/fixtures/h2_census.cc
)


target_include_directories(h2_census_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_census_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_compress_test
  test/core/end2end/fixtures/h2_compress.cc
)


target_include_directories(h2_compress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_compress_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_fakesec_test
  test/core/end2end/fixtures/h2_fakesec.cc
)


target_include_directories(h2_fakesec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_fakesec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(h2_fd_test
  test/core/end2end/fixtures/h2_fd.cc
)


target_include_directories(h2_fd_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_fd_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full_test
  test/core/end2end/fixtures/h2_full.cc
)


target_include_directories(h2_full_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(h2_full+pipe_test
  test/core/end2end/fixtures/h2_full+pipe.cc
)


target_include_directories(h2_full+pipe_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+pipe_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+trace_test
  test/core/end2end/fixtures/h2_full+trace.cc
)


target_include_directories(h2_full+trace_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+trace_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+workarounds_test
  test/core/end2end/fixtures/h2_full+workarounds.cc
)


target_include_directories(h2_full+workarounds_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+workarounds_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_http_proxy_test
  test/core/end2end/fixtures/h2_http_proxy.cc
)


target_include_directories(h2_http_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_http_proxy_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(h2_local_test
  test/core/end2end/fixtures/h2_local.cc
)


target_include_directories(h2_local_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_local_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_oauth2_test
  test/core/end2end/fixtures/h2_oauth2.cc
)


target_include_directories(h2_oauth2_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_oauth2_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_proxy_test
  test/core/end2end/fixtures/h2_proxy.cc
)


target_include_directories(h2_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_proxy_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_test
  test/core/end2end/fixtures/h2_sockpair.cc
)


target_include_directories(h2_sockpair_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair+trace_test
  test/core/end2end/fixtures/h2_sockpair+trace.cc
)


target_include_directories(h2_sockpair+trace_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair+trace_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_1byte_test
  test/core/end2end/fixtures/h2_sockpair_1byte.cc
)


target_include_directories(h2_sockpair_1byte_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair_1byte_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_test
  test/core/end2end/fixtures/h2_ssl.cc
)


target_include_directories(h2_ssl_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_ssl_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_proxy_test
  test/core/end2end/fixtures/h2_ssl_proxy.cc
)


target_include_directories(h2_ssl_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_ssl_proxy_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(h2_uds_test
  test/core/end2end/fixtures/h2_uds.cc
)


target_include_directories(h2_uds_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_uds_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(inproc_test
  test/core/end2end/fixtures/inproc.cc
)


target_include_directories(inproc_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(inproc_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_census_nosec_test
  test/core/end2end/fixtures/h2_census.cc
)


target_include_directories(h2_census_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_census_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_compress_nosec_test
  test/core/end2end/fixtures/h2_compress.cc
)


target_include_directories(h2_compress_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_compress_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(h2_fd_nosec_test
  test/core/end2end/fixtures/h2_fd.cc
)


target_include_directories(h2_fd_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_fd_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full_nosec_test
  test/core/end2end/fixtures/h2_full.cc
)


target_include_directories(h2_full_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX)

add_executable(h2_full+pipe_nosec_test
  test/core/end2end/fixtures/h2_full+pipe.cc
)


target_include_directories(h2_full+pipe_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+pipe_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+trace_nosec_test
  test/core/end2end/fixtures/h2_full+trace.cc
)


target_include_directories(h2_full+trace_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+trace_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+workarounds_nosec_test
  test/core/end2end/fixtures/h2_full+workarounds.cc
)


target_include_directories(h2_full+workarounds_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_full+workarounds_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_http_proxy_nosec_test
  test/core/end2end/fixtures/h2_http_proxy.cc
)


target_include_directories(h2_http_proxy_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_http_proxy_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_proxy_nosec_test
  test/core/end2end/fixtures/h2_proxy.cc
)


target_include_directories(h2_proxy_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_proxy_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_nosec_test
  test/core/end2end/fixtures/h2_sockpair.cc
)


target_include_directories(h2_sockpair_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair+trace_nosec_test
  test/core/end2end/fixtures/h2_sockpair+trace.cc
)


target_include_directories(h2_sockpair+trace_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair+trace_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_1byte_nosec_test
  test/core/end2end/fixtures/h2_sockpair_1byte.cc
)


target_include_directories(h2_sockpair_1byte_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_sockpair_1byte_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(h2_uds_nosec_test
  test/core/end2end/fixtures/h2_uds.cc
)


target_include_directories(h2_uds_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(h2_uds_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(inproc_nosec_test
  test/core/end2end/fixtures/inproc.cc
)


target_include_directories(inproc_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(inproc_nosec_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(resolver_component_test_unsecure
  test/cpp/naming/resolver_component_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(resolver_component_test_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(resolver_component_test_unsecure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  gpr_test_util
  grpc++_unsecure
  grpc_unsecure
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(resolver_component_test
  test/cpp/naming/resolver_component_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(resolver_component_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(resolver_component_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  gpr_test_util
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(resolver_component_tests_runner_invoker_unsecure
  test/cpp/naming/resolver_component_tests_runner_invoker.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(resolver_component_tests_runner_invoker_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(resolver_component_tests_runner_invoker_unsecure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  gpr_test_util
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(resolver_component_tests_runner_invoker
  test/cpp/naming/resolver_component_tests_runner_invoker.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(resolver_component_tests_runner_invoker
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(resolver_component_tests_runner_invoker
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  gpr_test_util
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(address_sorting_test_unsecure
  test/cpp/naming/address_sorting_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(address_sorting_test_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(address_sorting_test_unsecure
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util_unsecure
  grpc_test_util_unsecure
  gpr_test_util
  grpc++_unsecure
  grpc_unsecure
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(address_sorting_test
  test/cpp/naming/address_sorting_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(address_sorting_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(address_sorting_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  gpr_test_util
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)
if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_POSIX)

add_executable(cancel_ares_query_test
  test/cpp/naming/cancel_ares_query_test.cc
  third_party/googletest/googletest/src/gtest-all.cc
  third_party/googletest/googlemock/src/gmock-all.cc
)


target_include_directories(cancel_ares_query_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
  PRIVATE third_party/googletest/googletest/include
  PRIVATE third_party/googletest/googletest
  PRIVATE third_party/googletest/googlemock/include
  PRIVATE third_party/googletest/googlemock
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(cancel_ares_query_test
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_util
  grpc_test_util
  gpr_test_util
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif()
endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alts_credentials_fuzzer_one_entry
  test/core/security/alts_credentials_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(alts_credentials_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(alts_credentials_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(api_fuzzer_one_entry
  test/core/end2end/fuzzers/api_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(api_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(api_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(client_fuzzer_one_entry
  test/core/end2end/fuzzers/client_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(client_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(client_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_parser_fuzzer_test_one_entry
  test/core/transport/chttp2/hpack_parser_fuzzer_test.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(hpack_parser_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(hpack_parser_fuzzer_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_request_fuzzer_test_one_entry
  test/core/http/request_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(http_request_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(http_request_fuzzer_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_response_fuzzer_test_one_entry
  test/core/http/response_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(http_response_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(http_response_fuzzer_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_fuzzer_test_one_entry
  test/core/json/fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(json_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(json_fuzzer_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_response_test_one_entry
  test/core/nanopb/fuzzer_response.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(nanopb_fuzzer_response_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(nanopb_fuzzer_response_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_serverlist_test_one_entry
  test/core/nanopb/fuzzer_serverlist.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(nanopb_fuzzer_serverlist_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(nanopb_fuzzer_serverlist_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_decode_fuzzer_one_entry
  test/core/slice/percent_decode_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(percent_decode_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(percent_decode_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_encode_fuzzer_one_entry
  test/core/slice/percent_encode_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(percent_encode_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(percent_encode_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_fuzzer_one_entry
  test/core/end2end/fuzzers/server_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(server_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(server_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(ssl_server_fuzzer_one_entry
  test/core/security/ssl_server_fuzzer.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(ssl_server_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(ssl_server_fuzzer_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(uri_fuzzer_test_one_entry
  test/core/client_channel/uri_fuzzer_test.cc
  test/core/util/one_corpus_entry_fuzzer.cc
)


target_include_directories(uri_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${_gRPC_SSL_INCLUDE_DIR}
  PRIVATE ${_gRPC_PROTOBUF_INCLUDE_DIR}
  PRIVATE ${_gRPC_ZLIB_INCLUDE_DIR}
  PRIVATE ${_gRPC_BENCHMARK_INCLUDE_DIR}
  PRIVATE ${_gRPC_CARES_INCLUDE_DIR}
  PRIVATE ${_gRPC_GFLAGS_INCLUDE_DIR}
  PRIVATE ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
  PRIVATE ${_gRPC_NANOPB_INCLUDE_DIR}
)

target_link_libraries(uri_fuzzer_test_one_entry
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)







if (gRPC_INSTALL)
  install(EXPORT gRPCTargets
    DESTINATION ${gRPC_INSTALL_CMAKEDIR}
    NAMESPACE gRPC::
  )
endif()

foreach(_config gRPCConfig gRPCConfigVersion)
  configure_file(cmake/${_config}.cmake.in
    ${_config}.cmake @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_config}.cmake
    DESTINATION ${gRPC_INSTALL_CMAKEDIR}
  )
endforeach()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/etc/roots.pem
  DESTINATION ${gRPC_INSTALL_SHAREDIR})
