# GRPC global cmake file
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh
#
# Additionally, this is currently very experimental, and unsupported.
# Further work will happen on that file.
#
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.



cmake_minimum_required(VERSION 2.8)

set(PACKAGE_NAME      "grpc")
set(PACKAGE_VERSION   "1.1.0-dev")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} C CXX)

if (NOT MSVC)
  set(gRPC_INSTALL ON CACHE BOOL "Generate installation target")
else()
  set(gRPC_INSTALL OFF CACHE BOOL "Generate installation target")
endif()

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if (MSVC)
  add_definitions( -D_WIN32_WINNT=0x600 )
endif()

if (gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

if("${gRPC_ZLIB_PROVIDER}" STREQUAL "module")
  if(NOT ZLIB_ROOT_DIR)
    set(ZLIB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
  endif()
  set(ZLIB_INCLUDE_DIR "${ZLIB_ROOT_DIR}")
  if(EXISTS "${ZLIB_ROOT_DIR}/CMakeLists.txt")
      add_subdirectory(${ZLIB_ROOT_DIR} third_party/zlib)
      if(TARGET zlibstatic)
          set(_gRPC_ZLIB_LIBRARIES zlibstatic)
      endif()
  else()
      message(WARNING "gRPC_ZLIB_PROVIDER is \"module\" but ZLIB_ROOT_DIR is wrong")
  endif()
elseif("${gRPC_ZLIB_PROVIDER}" STREQUAL "package")
  find_package(ZLIB)
  if(TARGET ZLIB::ZLIB)
    set(_gRPC_ZLIB_LIBRARIES ZLIB::ZLIB)
  endif()
  set(_gRPC_FIND_ZLIB "if(NOT ZLIB_FOUND)\n  find_package(ZLIB)\nendif()")
endif()

if("${gRPC_PROTOBUF_PROVIDER}" STREQUAL "module")
  # Building the protobuf tests require gmock what is not part of a standard protobuf checkout.
  # Disable them unless they are explicitly requested from the cmake command line (when we assume
  # gmock is downloaded to the right location inside protobuf).
  if(NOT protobuf_BUILD_TESTS)
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build protobuf tests")
  endif()
  if(NOT PROTOBUF_ROOT_DIR)
    set(PROTOBUF_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf)
  endif()
  if(EXISTS "${PROTOBUF_ROOT_DIR}/cmake/CMakeLists.txt")
    set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "Link static runtime libraries")
    add_subdirectory(${PROTOBUF_ROOT_DIR}/cmake third_party/protobuf)
    if(TARGET ${_gRPC_PROTOBUF_LIBRARY_NAME})
      set(_gRPC_PROTOBUF_LIBRARIES ${_gRPC_PROTOBUF_LIBRARY_NAME})
    endif()
    if(TARGET libprotoc)
      set(_gRPC_PROTOBUF_PROTOC_LIBRARIES libprotoc)
    endif()
  else()
      message(WARNING "gRPC_PROTOBUF_PROVIDER is \"module\" but PROTOBUF_ROOT_DIR is wrong")
  endif()
elseif("${gRPC_PROTOBUF_PROVIDER}" STREQUAL "package")
  find_package(protobuf CONFIG)
  if(protobuf_FOUND)
    if(TARGET protobuf::${_gRPC_PROTOBUF_LIBRARY_NAME})
      set(_gRPC_PROTOBUF_LIBRARIES protobuf::${_gRPC_PROTOBUF_LIBRARY_NAME})
    endif()
    if(TARGET protobuf::libprotoc)
      set(_gRPC_PROTOBUF_PROTOC_LIBRARIES protobuf::libprotoc)
    endif()
    set(_gRPC_FIND_PROTOBUF "if(NOT protobuf_FOUND)\n  find_package(protobuf CONFIG)\nendif()")
  else()
    find_package(Protobuf MODULE)
    set(_gRPC_FIND_PROTOBUF "if(NOT Protobuf_FOUND)\n  find_package(Protobuf)\nendif()")
  endif()
endif()

if("${gRPC_SSL_PROVIDER}" STREQUAL "module")
  if(NOT BORINGSSL_ROOT_DIR)
    set(BORINGSSL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/boringssl)
  endif()
  if(EXISTS "${BORINGSSL_ROOT_DIR}/CMakeLists.txt")
    add_subdirectory(${BORINGSSL_ROOT_DIR} third_party/boringssl)
    if(TARGET ssl)
      set(_gRPC_SSL_LIBRARIES ssl)
    endif()
  else()
      message(WARNING "gRPC_SSL_PROVIDER is \"module\" but BORINGSSL_ROOT_DIR is wrong")
  endif()
elseif("${gRPC_SSL_PROVIDER}" STREQUAL "package")
  find_package(OpenSSL)
  if(TARGET OpenSSL::SSL)
    set(_gRPC_SSL_LIBRARIES OpenSSL::SSL)
  endif()
  set(_gRPC_FIND_SSL "if(NOT OpenSSL_FOUND)\n  find_package(OpenSSL)\nendif()")
endif()

if(NOT MSVC)
  set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -std=c11")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

if(WIN32 AND MSVC)
  set(_gRPC_BASELIB_LIBRARIES wsock32 ws2_32)
endif()

include(GNUInstallDirs)
if(NOT DEFINED CMAKE_INSTALL_CMAKEDIR)
  set(CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/gRPC")
endif()

  
add_library(gpr
  src/core/lib/profiling/basic_timers.c
  src/core/lib/profiling/stap_timers.c
  src/core/lib/support/alloc.c
  src/core/lib/support/avl.c
  src/core/lib/support/backoff.c
  src/core/lib/support/cmdline.c
  src/core/lib/support/cpu_iphone.c
  src/core/lib/support/cpu_linux.c
  src/core/lib/support/cpu_posix.c
  src/core/lib/support/cpu_windows.c
  src/core/lib/support/env_linux.c
  src/core/lib/support/env_posix.c
  src/core/lib/support/env_windows.c
  src/core/lib/support/histogram.c
  src/core/lib/support/host_port.c
  src/core/lib/support/log.c
  src/core/lib/support/log_android.c
  src/core/lib/support/log_linux.c
  src/core/lib/support/log_posix.c
  src/core/lib/support/log_windows.c
  src/core/lib/support/mpscq.c
  src/core/lib/support/murmur_hash.c
  src/core/lib/support/percent_encoding.c
  src/core/lib/support/slice.c
  src/core/lib/support/slice_buffer.c
  src/core/lib/support/stack_lockfree.c
  src/core/lib/support/string.c
  src/core/lib/support/string_posix.c
  src/core/lib/support/string_util_windows.c
  src/core/lib/support/string_windows.c
  src/core/lib/support/subprocess_posix.c
  src/core/lib/support/subprocess_windows.c
  src/core/lib/support/sync.c
  src/core/lib/support/sync_posix.c
  src/core/lib/support/sync_windows.c
  src/core/lib/support/thd.c
  src/core/lib/support/thd_posix.c
  src/core/lib/support/thd_windows.c
  src/core/lib/support/time.c
  src/core/lib/support/time_posix.c
  src/core/lib/support/time_precise.c
  src/core/lib/support/time_windows.c
  src/core/lib/support/tls_pthread.c
  src/core/lib/support/tmpfile_msys.c
  src/core/lib/support/tmpfile_posix.c
  src/core/lib/support/tmpfile_windows.c
  src/core/lib/support/wrap_memcpy.c
)

target_include_directories(gpr
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)


foreach(_hdr
  include/grpc/support/alloc.h
  include/grpc/support/atm.h
  include/grpc/support/atm_gcc_atomic.h
  include/grpc/support/atm_gcc_sync.h
  include/grpc/support/atm_windows.h
  include/grpc/support/avl.h
  include/grpc/support/cmdline.h
  include/grpc/support/cpu.h
  include/grpc/support/histogram.h
  include/grpc/support/host_port.h
  include/grpc/support/log.h
  include/grpc/support/log_windows.h
  include/grpc/support/port_platform.h
  include/grpc/support/slice.h
  include/grpc/support/slice_buffer.h
  include/grpc/support/string_util.h
  include/grpc/support/subprocess.h
  include/grpc/support/sync.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd.h
  include/grpc/support/time.h
  include/grpc/support/tls.h
  include/grpc/support/tls_gcc.h
  include/grpc/support/tls_msvc.h
  include/grpc/support/tls_pthread.h
  include/grpc/support/useful.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS gpr EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc
  src/core/lib/surface/init.c
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/http_client_filter.c
  src/core/lib/channel/http_server_filter.c
  src/core/lib/channel/message_size_filter.c
  src/core/lib/compression/compression.c
  src/core/lib/compression/message_compress.c
  src/core/lib/debug/trace.c
  src/core/lib/http/format_request.c
  src/core/lib/http/httpcli.c
  src/core/lib/http/parser.c
  src/core/lib/iomgr/closure.c
  src/core/lib/iomgr/combiner.c
  src/core/lib/iomgr/endpoint.c
  src/core/lib/iomgr/endpoint_pair_posix.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_and_epoll_posix.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_posix.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/surface/alarm.c
  src/core/lib/surface/api_trace.c
  src/core/lib/surface/byte_buffer.c
  src/core/lib/surface/byte_buffer_reader.c
  src/core/lib/surface/call.c
  src/core/lib/surface/call_details.c
  src/core/lib/surface/call_log_batch.c
  src/core/lib/surface/channel.c
  src/core/lib/surface/channel_init.c
  src/core/lib/surface/channel_ping.c
  src/core/lib/surface/channel_stack_type.c
  src/core/lib/surface/completion_queue.c
  src/core/lib/surface/event_string.c
  src/core/lib/surface/lame_client.c
  src/core/lib/surface/metadata_array.c
  src/core/lib/surface/server.c
  src/core/lib/surface/validate_metadata.c
  src/core/lib/surface/version.c
  src/core/lib/transport/byte_stream.c
  src/core/lib/transport/connectivity_state.c
  src/core/lib/transport/hashtable.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c
  src/core/ext/transport/chttp2/transport/bin_decoder.c
  src/core/ext/transport/chttp2/transport/bin_encoder.c
  src/core/ext/transport/chttp2/transport/chttp2_plugin.c
  src/core/ext/transport/chttp2/transport/chttp2_transport.c
  src/core/ext/transport/chttp2/transport/frame_data.c
  src/core/ext/transport/chttp2/transport/frame_goaway.c
  src/core/ext/transport/chttp2/transport/frame_ping.c
  src/core/ext/transport/chttp2/transport/frame_rst_stream.c
  src/core/ext/transport/chttp2/transport/frame_settings.c
  src/core/ext/transport/chttp2/transport/frame_window_update.c
  src/core/ext/transport/chttp2/transport/hpack_encoder.c
  src/core/ext/transport/chttp2/transport/hpack_parser.c
  src/core/ext/transport/chttp2/transport/hpack_table.c
  src/core/ext/transport/chttp2/transport/huffsyms.c
  src/core/ext/transport/chttp2/transport/incoming_metadata.c
  src/core/ext/transport/chttp2/transport/parsing.c
  src/core/ext/transport/chttp2/transport/status_conversion.c
  src/core/ext/transport/chttp2/transport/stream_lists.c
  src/core/ext/transport/chttp2/transport/stream_map.c
  src/core/ext/transport/chttp2/transport/varint.c
  src/core/ext/transport/chttp2/transport/writing.c
  src/core/ext/transport/chttp2/alpn/alpn.c
  src/core/lib/http/httpcli_security_connector.c
  src/core/lib/security/context/security_context.c
  src/core/lib/security/credentials/composite/composite_credentials.c
  src/core/lib/security/credentials/credentials.c
  src/core/lib/security/credentials/credentials_metadata.c
  src/core/lib/security/credentials/fake/fake_credentials.c
  src/core/lib/security/credentials/google_default/credentials_posix.c
  src/core/lib/security/credentials/google_default/credentials_windows.c
  src/core/lib/security/credentials/google_default/google_default_credentials.c
  src/core/lib/security/credentials/iam/iam_credentials.c
  src/core/lib/security/credentials/jwt/json_token.c
  src/core/lib/security/credentials/jwt/jwt_credentials.c
  src/core/lib/security/credentials/jwt/jwt_verifier.c
  src/core/lib/security/credentials/oauth2/oauth2_credentials.c
  src/core/lib/security/credentials/plugin/plugin_credentials.c
  src/core/lib/security/credentials/ssl/ssl_credentials.c
  src/core/lib/security/transport/client_auth_filter.c
  src/core/lib/security/transport/handshake.c
  src/core/lib/security/transport/secure_endpoint.c
  src/core/lib/security/transport/security_connector.c
  src/core/lib/security/transport/server_auth_filter.c
  src/core/lib/security/transport/tsi_error.c
  src/core/lib/security/util/b64.c
  src/core/lib/security/util/json_util.c
  src/core/lib/surface/init_secure.c
  src/core/lib/tsi/fake_transport_security.c
  src/core/lib/tsi/ssl_transport_security.c
  src/core/lib/tsi/transport_security.c
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.c
  src/core/ext/client_config/channel_connectivity.c
  src/core/ext/client_config/client_channel.c
  src/core/ext/client_config/client_channel_factory.c
  src/core/ext/client_config/client_config_plugin.c
  src/core/ext/client_config/connector.c
  src/core/ext/client_config/default_initial_connect_string.c
  src/core/ext/client_config/http_connect_handshaker.c
  src/core/ext/client_config/initial_connect_string.c
  src/core/ext/client_config/lb_policy.c
  src/core/ext/client_config/lb_policy_factory.c
  src/core/ext/client_config/lb_policy_registry.c
  src/core/ext/client_config/method_config.c
  src/core/ext/client_config/parse_address.c
  src/core/ext/client_config/resolver.c
  src/core/ext/client_config/resolver_factory.c
  src/core/ext/client_config/resolver_registry.c
  src/core/ext/client_config/resolver_result.c
  src/core/ext/client_config/subchannel.c
  src/core/ext/client_config/subchannel_index.c
  src/core/ext/client_config/uri_parser.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c
  src/core/ext/transport/chttp2/client/insecure/channel_create.c
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c
  src/core/ext/lb_policy/grpclb/grpclb.c
  src/core/ext/lb_policy/grpclb/load_balancer_api.c
  src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
  src/core/ext/lb_policy/pick_first/pick_first.c
  src/core/ext/lb_policy/round_robin/round_robin.c
  src/core/ext/resolver/dns/native/dns_resolver.c
  src/core/ext/resolver/sockaddr/sockaddr_resolver.c
  src/core/ext/load_reporting/load_reporting.c
  src/core/ext/load_reporting/load_reporting_filter.c
  src/core/ext/census/base_resources.c
  src/core/ext/census/context.c
  src/core/ext/census/gen/census.pb.c
  src/core/ext/census/gen/trace_context.pb.c
  src/core/ext/census/grpc_context.c
  src/core/ext/census/grpc_filter.c
  src/core/ext/census/grpc_plugin.c
  src/core/ext/census/initialize.c
  src/core/ext/census/mlog.c
  src/core/ext/census/operation.c
  src/core/ext/census/placeholders.c
  src/core/ext/census/resource.c
  src/core/ext/census/trace_context.c
  src/core/ext/census/tracing.c
  src/core/plugin_registry/grpc_plugin_registry.c
)

target_include_directories(grpc
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/grpc_security.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc_cronet
  src/core/lib/surface/init.c
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/http_client_filter.c
  src/core/lib/channel/http_server_filter.c
  src/core/lib/channel/message_size_filter.c
  src/core/lib/compression/compression.c
  src/core/lib/compression/message_compress.c
  src/core/lib/debug/trace.c
  src/core/lib/http/format_request.c
  src/core/lib/http/httpcli.c
  src/core/lib/http/parser.c
  src/core/lib/iomgr/closure.c
  src/core/lib/iomgr/combiner.c
  src/core/lib/iomgr/endpoint.c
  src/core/lib/iomgr/endpoint_pair_posix.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_and_epoll_posix.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_posix.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/surface/alarm.c
  src/core/lib/surface/api_trace.c
  src/core/lib/surface/byte_buffer.c
  src/core/lib/surface/byte_buffer_reader.c
  src/core/lib/surface/call.c
  src/core/lib/surface/call_details.c
  src/core/lib/surface/call_log_batch.c
  src/core/lib/surface/channel.c
  src/core/lib/surface/channel_init.c
  src/core/lib/surface/channel_ping.c
  src/core/lib/surface/channel_stack_type.c
  src/core/lib/surface/completion_queue.c
  src/core/lib/surface/event_string.c
  src/core/lib/surface/lame_client.c
  src/core/lib/surface/metadata_array.c
  src/core/lib/surface/server.c
  src/core/lib/surface/validate_metadata.c
  src/core/lib/surface/version.c
  src/core/lib/transport/byte_stream.c
  src/core/lib/transport/connectivity_state.c
  src/core/lib/transport/hashtable.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/core/ext/transport/cronet/client/secure/cronet_channel_create.c
  src/core/ext/transport/cronet/transport/cronet_api_dummy.c
  src/core/ext/transport/cronet/transport/cronet_transport.c
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.c
  src/core/ext/transport/chttp2/transport/bin_decoder.c
  src/core/ext/transport/chttp2/transport/bin_encoder.c
  src/core/ext/transport/chttp2/transport/chttp2_plugin.c
  src/core/ext/transport/chttp2/transport/chttp2_transport.c
  src/core/ext/transport/chttp2/transport/frame_data.c
  src/core/ext/transport/chttp2/transport/frame_goaway.c
  src/core/ext/transport/chttp2/transport/frame_ping.c
  src/core/ext/transport/chttp2/transport/frame_rst_stream.c
  src/core/ext/transport/chttp2/transport/frame_settings.c
  src/core/ext/transport/chttp2/transport/frame_window_update.c
  src/core/ext/transport/chttp2/transport/hpack_encoder.c
  src/core/ext/transport/chttp2/transport/hpack_parser.c
  src/core/ext/transport/chttp2/transport/hpack_table.c
  src/core/ext/transport/chttp2/transport/huffsyms.c
  src/core/ext/transport/chttp2/transport/incoming_metadata.c
  src/core/ext/transport/chttp2/transport/parsing.c
  src/core/ext/transport/chttp2/transport/status_conversion.c
  src/core/ext/transport/chttp2/transport/stream_lists.c
  src/core/ext/transport/chttp2/transport/stream_map.c
  src/core/ext/transport/chttp2/transport/varint.c
  src/core/ext/transport/chttp2/transport/writing.c
  src/core/ext/transport/chttp2/alpn/alpn.c
  src/core/ext/client_config/channel_connectivity.c
  src/core/ext/client_config/client_channel.c
  src/core/ext/client_config/client_channel_factory.c
  src/core/ext/client_config/client_config_plugin.c
  src/core/ext/client_config/connector.c
  src/core/ext/client_config/default_initial_connect_string.c
  src/core/ext/client_config/http_connect_handshaker.c
  src/core/ext/client_config/initial_connect_string.c
  src/core/ext/client_config/lb_policy.c
  src/core/ext/client_config/lb_policy_factory.c
  src/core/ext/client_config/lb_policy_registry.c
  src/core/ext/client_config/method_config.c
  src/core/ext/client_config/parse_address.c
  src/core/ext/client_config/resolver.c
  src/core/ext/client_config/resolver_factory.c
  src/core/ext/client_config/resolver_registry.c
  src/core/ext/client_config/resolver_result.c
  src/core/ext/client_config/subchannel.c
  src/core/ext/client_config/subchannel_index.c
  src/core/ext/client_config/uri_parser.c
  src/core/lib/http/httpcli_security_connector.c
  src/core/lib/security/context/security_context.c
  src/core/lib/security/credentials/composite/composite_credentials.c
  src/core/lib/security/credentials/credentials.c
  src/core/lib/security/credentials/credentials_metadata.c
  src/core/lib/security/credentials/fake/fake_credentials.c
  src/core/lib/security/credentials/google_default/credentials_posix.c
  src/core/lib/security/credentials/google_default/credentials_windows.c
  src/core/lib/security/credentials/google_default/google_default_credentials.c
  src/core/lib/security/credentials/iam/iam_credentials.c
  src/core/lib/security/credentials/jwt/json_token.c
  src/core/lib/security/credentials/jwt/jwt_credentials.c
  src/core/lib/security/credentials/jwt/jwt_verifier.c
  src/core/lib/security/credentials/oauth2/oauth2_credentials.c
  src/core/lib/security/credentials/plugin/plugin_credentials.c
  src/core/lib/security/credentials/ssl/ssl_credentials.c
  src/core/lib/security/transport/client_auth_filter.c
  src/core/lib/security/transport/handshake.c
  src/core/lib/security/transport/secure_endpoint.c
  src/core/lib/security/transport/security_connector.c
  src/core/lib/security/transport/server_auth_filter.c
  src/core/lib/security/transport/tsi_error.c
  src/core/lib/security/util/b64.c
  src/core/lib/security/util/json_util.c
  src/core/lib/surface/init_secure.c
  src/core/lib/tsi/fake_transport_security.c
  src/core/lib/tsi/ssl_transport_security.c
  src/core/lib/tsi/transport_security.c
  src/core/plugin_registry/grpc_cronet_plugin_registry.c
)

target_include_directories(grpc_cronet
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_cronet
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/grpc_cronet.h
  include/grpc/grpc_security.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc_cronet EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc_unsecure
  src/core/lib/surface/init.c
  src/core/lib/surface/init_unsecure.c
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/http_client_filter.c
  src/core/lib/channel/http_server_filter.c
  src/core/lib/channel/message_size_filter.c
  src/core/lib/compression/compression.c
  src/core/lib/compression/message_compress.c
  src/core/lib/debug/trace.c
  src/core/lib/http/format_request.c
  src/core/lib/http/httpcli.c
  src/core/lib/http/parser.c
  src/core/lib/iomgr/closure.c
  src/core/lib/iomgr/combiner.c
  src/core/lib/iomgr/endpoint.c
  src/core/lib/iomgr/endpoint_pair_posix.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_and_epoll_posix.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_posix.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/surface/alarm.c
  src/core/lib/surface/api_trace.c
  src/core/lib/surface/byte_buffer.c
  src/core/lib/surface/byte_buffer_reader.c
  src/core/lib/surface/call.c
  src/core/lib/surface/call_details.c
  src/core/lib/surface/call_log_batch.c
  src/core/lib/surface/channel.c
  src/core/lib/surface/channel_init.c
  src/core/lib/surface/channel_ping.c
  src/core/lib/surface/channel_stack_type.c
  src/core/lib/surface/completion_queue.c
  src/core/lib/surface/event_string.c
  src/core/lib/surface/lame_client.c
  src/core/lib/surface/metadata_array.c
  src/core/lib/surface/server.c
  src/core/lib/surface/validate_metadata.c
  src/core/lib/surface/version.c
  src/core/lib/transport/byte_stream.c
  src/core/lib/transport/connectivity_state.c
  src/core/lib/transport/hashtable.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c
  src/core/ext/transport/chttp2/transport/bin_decoder.c
  src/core/ext/transport/chttp2/transport/bin_encoder.c
  src/core/ext/transport/chttp2/transport/chttp2_plugin.c
  src/core/ext/transport/chttp2/transport/chttp2_transport.c
  src/core/ext/transport/chttp2/transport/frame_data.c
  src/core/ext/transport/chttp2/transport/frame_goaway.c
  src/core/ext/transport/chttp2/transport/frame_ping.c
  src/core/ext/transport/chttp2/transport/frame_rst_stream.c
  src/core/ext/transport/chttp2/transport/frame_settings.c
  src/core/ext/transport/chttp2/transport/frame_window_update.c
  src/core/ext/transport/chttp2/transport/hpack_encoder.c
  src/core/ext/transport/chttp2/transport/hpack_parser.c
  src/core/ext/transport/chttp2/transport/hpack_table.c
  src/core/ext/transport/chttp2/transport/huffsyms.c
  src/core/ext/transport/chttp2/transport/incoming_metadata.c
  src/core/ext/transport/chttp2/transport/parsing.c
  src/core/ext/transport/chttp2/transport/status_conversion.c
  src/core/ext/transport/chttp2/transport/stream_lists.c
  src/core/ext/transport/chttp2/transport/stream_map.c
  src/core/ext/transport/chttp2/transport/varint.c
  src/core/ext/transport/chttp2/transport/writing.c
  src/core/ext/transport/chttp2/alpn/alpn.c
  src/core/ext/transport/chttp2/client/insecure/channel_create.c
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c
  src/core/ext/client_config/channel_connectivity.c
  src/core/ext/client_config/client_channel.c
  src/core/ext/client_config/client_channel_factory.c
  src/core/ext/client_config/client_config_plugin.c
  src/core/ext/client_config/connector.c
  src/core/ext/client_config/default_initial_connect_string.c
  src/core/ext/client_config/http_connect_handshaker.c
  src/core/ext/client_config/initial_connect_string.c
  src/core/ext/client_config/lb_policy.c
  src/core/ext/client_config/lb_policy_factory.c
  src/core/ext/client_config/lb_policy_registry.c
  src/core/ext/client_config/method_config.c
  src/core/ext/client_config/parse_address.c
  src/core/ext/client_config/resolver.c
  src/core/ext/client_config/resolver_factory.c
  src/core/ext/client_config/resolver_registry.c
  src/core/ext/client_config/resolver_result.c
  src/core/ext/client_config/subchannel.c
  src/core/ext/client_config/subchannel_index.c
  src/core/ext/client_config/uri_parser.c
  src/core/ext/resolver/dns/native/dns_resolver.c
  src/core/ext/resolver/sockaddr/sockaddr_resolver.c
  src/core/ext/load_reporting/load_reporting.c
  src/core/ext/load_reporting/load_reporting_filter.c
  src/core/ext/lb_policy/grpclb/grpclb.c
  src/core/ext/lb_policy/grpclb/load_balancer_api.c
  src/core/ext/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
  src/core/ext/lb_policy/pick_first/pick_first.c
  src/core/ext/lb_policy/round_robin/round_robin.c
  src/core/ext/census/base_resources.c
  src/core/ext/census/context.c
  src/core/ext/census/gen/census.pb.c
  src/core/ext/census/gen/trace_context.pb.c
  src/core/ext/census/grpc_context.c
  src/core/ext/census/grpc_filter.c
  src/core/ext/census/grpc_plugin.c
  src/core/ext/census/initialize.c
  src/core/ext/census/mlog.c
  src/core/ext/census/operation.c
  src/core/ext/census/placeholders.c
  src/core/ext/census/resource.c
  src/core/ext/census/trace_context.c
  src/core/ext/census/tracing.c
  src/core/plugin_registry/grpc_unsecure_plugin_registry.c
)

target_include_directories(grpc_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  gpr
)

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
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
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/rpc_method.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/slice_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/http_client_filter.c
  src/core/lib/channel/http_server_filter.c
  src/core/lib/channel/message_size_filter.c
  src/core/lib/compression/compression.c
  src/core/lib/compression/message_compress.c
  src/core/lib/debug/trace.c
  src/core/lib/http/format_request.c
  src/core/lib/http/httpcli.c
  src/core/lib/http/parser.c
  src/core/lib/iomgr/closure.c
  src/core/lib/iomgr/combiner.c
  src/core/lib/iomgr/endpoint.c
  src/core/lib/iomgr/endpoint_pair_posix.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_and_epoll_posix.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_posix.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/surface/alarm.c
  src/core/lib/surface/api_trace.c
  src/core/lib/surface/byte_buffer.c
  src/core/lib/surface/byte_buffer_reader.c
  src/core/lib/surface/call.c
  src/core/lib/surface/call_details.c
  src/core/lib/surface/call_log_batch.c
  src/core/lib/surface/channel.c
  src/core/lib/surface/channel_init.c
  src/core/lib/surface/channel_ping.c
  src/core/lib/surface/channel_stack_type.c
  src/core/lib/surface/completion_queue.c
  src/core/lib/surface/event_string.c
  src/core/lib/surface/lame_client.c
  src/core/lib/surface/metadata_array.c
  src/core/lib/surface/server.c
  src/core/lib/surface/validate_metadata.c
  src/core/lib/surface/version.c
  src/core/lib/transport/byte_stream.c
  src/core/lib/transport/connectivity_state.c
  src/core/lib/transport/hashtable.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/cpp/codegen/codegen_init.cc
)

target_include_directories(grpc++
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc++
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
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
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/impl/call.h
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
  include/grpc++/impl/sync.h
  include/grpc++/impl/sync_cxx11.h
  include/grpc++/impl/sync_no_cxx11.h
  include/grpc++/impl/thd.h
  include/grpc++/impl/thd_cxx11.h
  include/grpc++/impl/thd_no_cxx11.h
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
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
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
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/status_helper.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync.h
  include/grpc++/impl/codegen/sync_cxx11.h
  include/grpc++/impl/codegen/sync_no_cxx11.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc++ EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc++_reflection
  src/cpp/ext/proto_server_reflection.cc
  src/cpp/ext/proto_server_reflection_plugin.cc
  src/cpp/ext/reflection.grpc.pb.cc
  src/cpp/ext/reflection.pb.cc
)

target_include_directories(grpc++_reflection
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc++_reflection
  grpc++
)

foreach(_hdr
  include/grpc++/ext/proto_server_reflection_plugin.h
  include/grpc++/ext/reflection.grpc.pb.h
  include/grpc++/ext/reflection.pb.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
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
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/status_helper.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync.h
  include/grpc++/impl/codegen/sync_cxx11.h
  include/grpc++/impl/codegen/sync_no_cxx11.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc++_reflection EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
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
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/rpc_method.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/slice_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/http_client_filter.c
  src/core/lib/channel/http_server_filter.c
  src/core/lib/channel/message_size_filter.c
  src/core/lib/compression/compression.c
  src/core/lib/compression/message_compress.c
  src/core/lib/debug/trace.c
  src/core/lib/http/format_request.c
  src/core/lib/http/httpcli.c
  src/core/lib/http/parser.c
  src/core/lib/iomgr/closure.c
  src/core/lib/iomgr/combiner.c
  src/core/lib/iomgr/endpoint.c
  src/core/lib/iomgr/endpoint_pair_posix.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_and_epoll_posix.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_posix.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/surface/alarm.c
  src/core/lib/surface/api_trace.c
  src/core/lib/surface/byte_buffer.c
  src/core/lib/surface/byte_buffer_reader.c
  src/core/lib/surface/call.c
  src/core/lib/surface/call_details.c
  src/core/lib/surface/call_log_batch.c
  src/core/lib/surface/channel.c
  src/core/lib/surface/channel_init.c
  src/core/lib/surface/channel_ping.c
  src/core/lib/surface/channel_stack_type.c
  src/core/lib/surface/completion_queue.c
  src/core/lib/surface/event_string.c
  src/core/lib/surface/lame_client.c
  src/core/lib/surface/metadata_array.c
  src/core/lib/surface/server.c
  src/core/lib/surface/validate_metadata.c
  src/core/lib/surface/version.c
  src/core/lib/transport/byte_stream.c
  src/core/lib/transport/connectivity_state.c
  src/core/lib/transport/hashtable.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/cpp/codegen/codegen_init.cc
)

target_include_directories(grpc++_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc++_unsecure
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
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
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/impl/call.h
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
  include/grpc++/impl/sync.h
  include/grpc++/impl/sync_cxx11.h
  include/grpc++/impl/sync_no_cxx11.h
  include/grpc++/impl/thd.h
  include/grpc++/impl/thd_cxx11.h
  include/grpc++/impl/thd_no_cxx11.h
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
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
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
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/status_helper.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync.h
  include/grpc++/impl/codegen/sync_cxx11.h
  include/grpc++/impl/codegen/sync_no_cxx11.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc++_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc_plugin_support
  src/compiler/cpp_generator.cc
  src/compiler/csharp_generator.cc
  src/compiler/node_generator.cc
  src/compiler/objective_c_generator.cc
  src/compiler/python_generator.cc
  src/compiler/ruby_generator.cc
)

target_include_directories(grpc_plugin_support
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_plugin_support
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
)

foreach(_hdr
  include/grpc++/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

  
if (gRPC_INSTALL)
  install(TARGETS grpc_plugin_support EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

  
add_library(grpc_csharp_ext
  src/csharp/ext/grpc_csharp_ext.c
)

target_include_directories(grpc_csharp_ext
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_csharp_ext
  grpc
  gpr
)


  
if (gRPC_INSTALL)
  install(TARGETS grpc_csharp_ext EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()



add_executable(gen_hpack_tables
  tools/codegen/core/gen_hpack_tables.c
)

target_include_directories(gen_hpack_tables
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(gen_hpack_tables
  gpr
  grpc
)


if (gRPC_INSTALL)
  install(TARGETS gen_hpack_tables EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(gen_legal_metadata_characters
  tools/codegen/core/gen_legal_metadata_characters.c
)

target_include_directories(gen_legal_metadata_characters
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)



if (gRPC_INSTALL)
  install(TARGETS gen_legal_metadata_characters EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(gen_percent_encoding_tables
  tools/codegen/core/gen_percent_encoding_tables.c
)

target_include_directories(gen_percent_encoding_tables
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)



if (gRPC_INSTALL)
  install(TARGETS gen_percent_encoding_tables EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_create_jwt
  test/core/security/create_jwt.c
)

target_include_directories(grpc_create_jwt
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_create_jwt
  ${_gRPC_SSL_LIBRARIES}
  grpc
  gpr
)


if (gRPC_INSTALL)
  install(TARGETS grpc_create_jwt EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_print_google_default_creds_token
  test/core/security/print_google_default_creds_token.c
)

target_include_directories(grpc_print_google_default_creds_token
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_print_google_default_creds_token
  grpc
  gpr
)


if (gRPC_INSTALL)
  install(TARGETS grpc_print_google_default_creds_token EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_verify_jwt
  test/core/security/verify_jwt.c
)

target_include_directories(grpc_verify_jwt
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_verify_jwt
  grpc
  gpr
)


if (gRPC_INSTALL)
  install(TARGETS grpc_verify_jwt EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_cpp_plugin
  src/compiler/cpp_plugin.cc
)

target_include_directories(grpc_cpp_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_cpp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_cpp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_csharp_plugin
  src/compiler/csharp_plugin.cc
)

target_include_directories(grpc_csharp_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_csharp_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_csharp_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_node_plugin
  src/compiler/node_plugin.cc
)

target_include_directories(grpc_node_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_node_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_node_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_objective_c_plugin
  src/compiler/objective_c_plugin.cc
)

target_include_directories(grpc_objective_c_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_objective_c_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_objective_c_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_python_plugin
  src/compiler/python_plugin.cc
)

target_include_directories(grpc_python_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_python_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_python_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


add_executable(grpc_ruby_plugin
  src/compiler/ruby_plugin.cc
)

target_include_directories(grpc_ruby_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
)

target_link_libraries(grpc_ruby_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_ruby_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()








if (gRPC_INSTALL)
  install(EXPORT gRPCTargets
    DESTINATION ${CMAKE_INSTALL_CMAKEDIR}
    NAMESPACE gRPC::
  )
endif()

foreach(_config gRPCConfig gRPCConfigVersion)
  configure_file(tools/cmake/${_config}.cmake.in
    ${_config}.cmake @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_config}.cmake
    DESTINATION ${CMAKE_INSTALL_CMAKEDIR}
  )
endforeach()
