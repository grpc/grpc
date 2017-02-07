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
set(PACKAGE_VERSION   "1.1.2")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} C CXX)

# Options
option(gRPC_BUILD_TESTS "Build tests" OFF)

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

set(gRPC_GFLAGS_PROVIDER "module" CACHE STRING "Provider of gflags library")
set_property(CACHE gRPC_GFLAGS_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if (MSVC)
  add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # needed to compile boringssl
  add_definitions(/wd4464 /wd4623 /wd4668 /wd4701 /wd4702 /wd4777 /wd5027)
  # needed to compile protobuf
  add_definitions(/wd4065 /wd4506)
  # TODO(jtattermusch): revisit C4267 occurrences throughout the code
  add_definitions(/wd4267)
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
    if(TARGET protoc)
      set(_gRPC_PROTOBUF_PROTOC protoc)
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
    if(TARGET protobuf::protoc)
      set(_gRPC_PROTOBUF_PROTOC protobuf::protoc)
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

if("${gRPC_GFLAGS_PROVIDER}" STREQUAL "module")
  if(NOT GFLAGS_ROOT_DIR)
    set(GFLAGS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gflags)
  endif()
  if(EXISTS "${GFLAGS_ROOT_DIR}/CMakeLists.txt")
      add_subdirectory(${GFLAGS_ROOT_DIR} third_party/gflags)
      if(TARGET gflags_static)
          set(_gRPC_GFLAGS_LIBRARIES gflags_static)
      endif()
  else()
      message(WARNING "gRPC_GFLAGS_PROVIDER is \"module\" but GFLAGS_ROOT_DIR is wrong")
  endif()
elseif("${gRPC_GFLAGS_PROVIDER}" STREQUAL "package")
  find_package(gflags)
  if(TARGET gflags::gflags)
    set(_gRPC_GFLAGS_LIBRARIES gflags::gflags)
  endif()
  set(_gRPC_FIND_GFLAGS "if(NOT gflags_FOUND)\n  find_package(gflags)\nendif()")
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

  set(_protobuf_include_path -I .)
  foreach(FIL ${ARGN})
    get_filename_component(ABS_FIL ${FIL} ABSOLUTE)
    get_filename_component(FIL_WE ${FIL} NAME_WE)
    file(RELATIVE_PATH REL_FIL ${CMAKE_SOURCE_DIR} ${ABS_FIL})
    get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
    set(RELFIL_WE "${REL_DIR}/${FIL_WE}")
    
    add_custom_command(
      OUTPUT "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
             "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
      COMMAND ${_gRPC_PROTOBUF_PROTOC}
      ARGS --grpc_out=${_gRPC_PROTO_GENS_DIR}
           --cpp_out=${_gRPC_PROTO_GENS_DIR}
           --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
           ${_protobuf_include_path}
           ${REL_FIL}
      DEPENDS ${ABS_FIL} ${_gRPC_PROTOBUF_PROTOC} grpc_cpp_plugin
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Running gRPC C++ protocol buffer compiler on ${FIL}"
      VERBATIM)
      
      set_source_files_properties("${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc" "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h" PROPERTIES GENERATED TRUE)
  endforeach()
endfunction()


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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  include/grpc/impl/codegen/gpr_slice.h
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

if (gRPC_BUILD_TESTS)

add_library(gpr_test_util
  test/core/util/test_config.c
)


target_include_directories(gpr_test_util
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)

add_library(grpc
  src/core/lib/surface/init.c
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/handshaker_factory.c
  src/core/lib/channel/handshaker_registry.c
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
  src/core/lib/iomgr/endpoint_pair_uv.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_uv.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_uv.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_uv.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_uv.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/resource_quota.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_mutator.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_utils_uv.c
  src/core/lib/iomgr/socket_utils_windows.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_uv.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_uv.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_uv.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer_generic.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/timer_uv.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_cv.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_uv.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/slice/percent_encoding.c
  src/core/lib/slice/slice.c
  src/core/lib/slice/slice_buffer.c
  src/core/lib/slice/slice_string_helpers.c
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
  src/core/lib/transport/mdstr_hash_table.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/pid_controller.c
  src/core/lib/transport/service_config.c
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
  src/core/lib/security/credentials/google_default/credentials_generic.c
  src/core/lib/security/credentials/google_default/google_default_credentials.c
  src/core/lib/security/credentials/iam/iam_credentials.c
  src/core/lib/security/credentials/jwt/json_token.c
  src/core/lib/security/credentials/jwt/jwt_credentials.c
  src/core/lib/security/credentials/jwt/jwt_verifier.c
  src/core/lib/security/credentials/oauth2/oauth2_credentials.c
  src/core/lib/security/credentials/plugin/plugin_credentials.c
  src/core/lib/security/credentials/ssl/ssl_credentials.c
  src/core/lib/security/transport/client_auth_filter.c
  src/core/lib/security/transport/secure_endpoint.c
  src/core/lib/security/transport/security_connector.c
  src/core/lib/security/transport/security_handshaker.c
  src/core/lib/security/transport/server_auth_filter.c
  src/core/lib/security/transport/tsi_error.c
  src/core/lib/security/util/b64.c
  src/core/lib/security/util/json_util.c
  src/core/lib/surface/init_secure.c
  src/core/lib/tsi/fake_transport_security.c
  src/core/lib/tsi/ssl_transport_security.c
  src/core/lib/tsi/transport_security.c
  src/core/ext/transport/chttp2/server/chttp2_server.c
  src/core/ext/transport/chttp2/client/secure/secure_channel_create.c
  src/core/ext/client_channel/channel_connectivity.c
  src/core/ext/client_channel/client_channel.c
  src/core/ext/client_channel/client_channel_factory.c
  src/core/ext/client_channel/client_channel_plugin.c
  src/core/ext/client_channel/connector.c
  src/core/ext/client_channel/default_initial_connect_string.c
  src/core/ext/client_channel/http_connect_handshaker.c
  src/core/ext/client_channel/initial_connect_string.c
  src/core/ext/client_channel/lb_policy.c
  src/core/ext/client_channel/lb_policy_factory.c
  src/core/ext/client_channel/lb_policy_registry.c
  src/core/ext/client_channel/parse_address.c
  src/core/ext/client_channel/resolver.c
  src/core/ext/client_channel/resolver_factory.c
  src/core/ext/client_channel/resolver_registry.c
  src/core/ext/client_channel/subchannel.c
  src/core/ext/client_channel/subchannel_index.c
  src/core/ext/client_channel/uri_parser.c
  src/core/ext/transport/chttp2/client/chttp2_connector.c
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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
  src/core/lib/channel/handshaker_factory.c
  src/core/lib/channel/handshaker_registry.c
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
  src/core/lib/iomgr/endpoint_pair_uv.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_uv.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_uv.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_uv.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_uv.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/resource_quota.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_mutator.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_utils_uv.c
  src/core/lib/iomgr/socket_utils_windows.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_uv.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_uv.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_uv.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer_generic.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/timer_uv.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_cv.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_uv.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/slice/percent_encoding.c
  src/core/lib/slice/slice.c
  src/core/lib/slice/slice_buffer.c
  src/core/lib/slice/slice_string_helpers.c
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
  src/core/lib/transport/mdstr_hash_table.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/pid_controller.c
  src/core/lib/transport/service_config.c
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
  src/core/ext/client_channel/channel_connectivity.c
  src/core/ext/client_channel/client_channel.c
  src/core/ext/client_channel/client_channel_factory.c
  src/core/ext/client_channel/client_channel_plugin.c
  src/core/ext/client_channel/connector.c
  src/core/ext/client_channel/default_initial_connect_string.c
  src/core/ext/client_channel/http_connect_handshaker.c
  src/core/ext/client_channel/initial_connect_string.c
  src/core/ext/client_channel/lb_policy.c
  src/core/ext/client_channel/lb_policy_factory.c
  src/core/ext/client_channel/lb_policy_registry.c
  src/core/ext/client_channel/parse_address.c
  src/core/ext/client_channel/resolver.c
  src/core/ext/client_channel/resolver_factory.c
  src/core/ext/client_channel/resolver_registry.c
  src/core/ext/client_channel/subchannel.c
  src/core/ext/client_channel/subchannel_index.c
  src/core/ext/client_channel/uri_parser.c
  src/core/lib/http/httpcli_security_connector.c
  src/core/lib/security/context/security_context.c
  src/core/lib/security/credentials/composite/composite_credentials.c
  src/core/lib/security/credentials/credentials.c
  src/core/lib/security/credentials/credentials_metadata.c
  src/core/lib/security/credentials/fake/fake_credentials.c
  src/core/lib/security/credentials/google_default/credentials_generic.c
  src/core/lib/security/credentials/google_default/google_default_credentials.c
  src/core/lib/security/credentials/iam/iam_credentials.c
  src/core/lib/security/credentials/jwt/json_token.c
  src/core/lib/security/credentials/jwt/jwt_credentials.c
  src/core/lib/security/credentials/jwt/jwt_verifier.c
  src/core/lib/security/credentials/oauth2/oauth2_credentials.c
  src/core/lib/security/credentials/plugin/plugin_credentials.c
  src/core/lib/security/credentials/ssl/ssl_credentials.c
  src/core/lib/security/transport/client_auth_filter.c
  src/core/lib/security/transport/secure_endpoint.c
  src/core/lib/security/transport/security_connector.c
  src/core/lib/security/transport/security_handshaker.c
  src/core/lib/security/transport/server_auth_filter.c
  src/core/lib/security/transport/tsi_error.c
  src/core/lib/security/util/b64.c
  src/core/lib/security/util/json_util.c
  src/core/lib/surface/init_secure.c
  src/core/lib/tsi/fake_transport_security.c
  src/core/lib/tsi/ssl_transport_security.c
  src/core/lib/tsi/transport_security.c
  src/core/ext/transport/chttp2/client/chttp2_connector.c
  src/core/plugin_registry/grpc_cronet_plugin_registry.c
)


target_include_directories(grpc_cronet
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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

if (gRPC_BUILD_TESTS)

add_library(grpc_test_util
  test/core/end2end/data/client_certs.c
  test/core/end2end/data/server1_cert.c
  test/core/end2end/data/server1_key.c
  test/core/end2end/data/test_root_cert.c
  test/core/security/oauth2_utils.c
  test/core/end2end/cq_verifier.c
  test/core/end2end/fake_resolver.c
  test/core/end2end/fixtures/http_proxy.c
  test/core/end2end/fixtures/proxy.c
  test/core/iomgr/endpoint_tests.c
  test/core/util/grpc_profiler.c
  test/core/util/memory_counters.c
  test/core/util/mock_endpoint.c
  test/core/util/parse_hexstring.c
  test/core/util/passthru_endpoint.c
  test/core/util/port_posix.c
  test/core/util/port_server_client.c
  test/core/util/port_uv.c
  test/core/util/port_windows.c
  test/core/util/slice_splitter.c
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/handshaker_factory.c
  src/core/lib/channel/handshaker_registry.c
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
  src/core/lib/iomgr/endpoint_pair_uv.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_uv.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_uv.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_uv.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_uv.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/resource_quota.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_mutator.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_utils_uv.c
  src/core/lib/iomgr/socket_utils_windows.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_uv.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_uv.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_uv.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer_generic.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/timer_uv.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_cv.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_uv.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/slice/percent_encoding.c
  src/core/lib/slice/slice.c
  src/core/lib/slice/slice_buffer.c
  src/core/lib/slice/slice_string_helpers.c
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
  src/core/lib/transport/mdstr_hash_table.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/pid_controller.c
  src/core/lib/transport/service_config.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
)


target_include_directories(grpc_test_util
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_test_util
  gpr_test_util
  gpr
  grpc
)

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(grpc_test_util_unsecure
  test/core/end2end/cq_verifier.c
  test/core/end2end/fake_resolver.c
  test/core/end2end/fixtures/http_proxy.c
  test/core/end2end/fixtures/proxy.c
  test/core/iomgr/endpoint_tests.c
  test/core/util/grpc_profiler.c
  test/core/util/memory_counters.c
  test/core/util/mock_endpoint.c
  test/core/util/parse_hexstring.c
  test/core/util/passthru_endpoint.c
  test/core/util/port_posix.c
  test/core/util/port_server_client.c
  test/core/util/port_uv.c
  test/core/util/port_windows.c
  test/core/util/slice_splitter.c
)


target_include_directories(grpc_test_util_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_test_util_unsecure
  gpr
  gpr_test_util
  grpc_unsecure
  grpc
)


endif (gRPC_BUILD_TESTS)

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
  src/core/lib/channel/handshaker_factory.c
  src/core/lib/channel/handshaker_registry.c
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
  src/core/lib/iomgr/endpoint_pair_uv.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_uv.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_uv.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_uv.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_uv.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/resource_quota.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_mutator.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_utils_uv.c
  src/core/lib/iomgr/socket_utils_windows.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_uv.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_uv.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_uv.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer_generic.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/timer_uv.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_cv.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_uv.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/slice/percent_encoding.c
  src/core/lib/slice/slice.c
  src/core/lib/slice/slice_buffer.c
  src/core/lib/slice/slice_string_helpers.c
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
  src/core/lib/transport/mdstr_hash_table.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/pid_controller.c
  src/core/lib/transport/service_config.c
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
  src/core/ext/transport/chttp2/server/chttp2_server.c
  src/core/ext/transport/chttp2/client/insecure/channel_create.c
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c
  src/core/ext/transport/chttp2/client/chttp2_connector.c
  src/core/ext/client_channel/channel_connectivity.c
  src/core/ext/client_channel/client_channel.c
  src/core/ext/client_channel/client_channel_factory.c
  src/core/ext/client_channel/client_channel_plugin.c
  src/core/ext/client_channel/connector.c
  src/core/ext/client_channel/default_initial_connect_string.c
  src/core/ext/client_channel/http_connect_handshaker.c
  src/core/ext/client_channel/initial_connect_string.c
  src/core/ext/client_channel/lb_policy.c
  src/core/ext/client_channel/lb_policy_factory.c
  src/core/ext/client_channel/lb_policy_registry.c
  src/core/ext/client_channel/parse_address.c
  src/core/ext/client_channel/resolver.c
  src/core/ext/client_channel/resolver_factory.c
  src/core/ext/client_channel/resolver_registry.c
  src/core/ext/client_channel/subchannel.c
  src/core/ext/client_channel/subchannel_index.c
  src/core/ext/client_channel/uri_parser.c
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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

if (gRPC_BUILD_TESTS)

add_library(reconnect_server
  test/core/util/reconnect_server.c
)


target_include_directories(reconnect_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(reconnect_server
  test_tcp_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(test_tcp_server
  test/core/util/test_tcp_server.c
)


target_include_directories(test_tcp_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(test_tcp_server
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
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/slice_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
)


target_include_directories(grpc++
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc++
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_PROTOBUF_LIBRARIES}
  grpc
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
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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
  install(TARGETS grpc++ EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()


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
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/slice_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
  src/core/ext/transport/chttp2/client/insecure/channel_create.c
  src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c
  src/core/ext/transport/chttp2/client/chttp2_connector.c
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
  src/core/lib/channel/channel_args.c
  src/core/lib/channel/channel_stack.c
  src/core/lib/channel/channel_stack_builder.c
  src/core/lib/channel/compress_filter.c
  src/core/lib/channel/connected_channel.c
  src/core/lib/channel/deadline_filter.c
  src/core/lib/channel/handshaker.c
  src/core/lib/channel/handshaker_factory.c
  src/core/lib/channel/handshaker_registry.c
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
  src/core/lib/iomgr/endpoint_pair_uv.c
  src/core/lib/iomgr/endpoint_pair_windows.c
  src/core/lib/iomgr/error.c
  src/core/lib/iomgr/ev_epoll_linux.c
  src/core/lib/iomgr/ev_poll_posix.c
  src/core/lib/iomgr/ev_posix.c
  src/core/lib/iomgr/exec_ctx.c
  src/core/lib/iomgr/executor.c
  src/core/lib/iomgr/iocp_windows.c
  src/core/lib/iomgr/iomgr.c
  src/core/lib/iomgr/iomgr_posix.c
  src/core/lib/iomgr/iomgr_uv.c
  src/core/lib/iomgr/iomgr_windows.c
  src/core/lib/iomgr/load_file.c
  src/core/lib/iomgr/network_status_tracker.c
  src/core/lib/iomgr/polling_entity.c
  src/core/lib/iomgr/pollset_set_uv.c
  src/core/lib/iomgr/pollset_set_windows.c
  src/core/lib/iomgr/pollset_uv.c
  src/core/lib/iomgr/pollset_windows.c
  src/core/lib/iomgr/resolve_address_posix.c
  src/core/lib/iomgr/resolve_address_uv.c
  src/core/lib/iomgr/resolve_address_windows.c
  src/core/lib/iomgr/resource_quota.c
  src/core/lib/iomgr/sockaddr_utils.c
  src/core/lib/iomgr/socket_mutator.c
  src/core/lib/iomgr/socket_utils_common_posix.c
  src/core/lib/iomgr/socket_utils_linux.c
  src/core/lib/iomgr/socket_utils_posix.c
  src/core/lib/iomgr/socket_utils_uv.c
  src/core/lib/iomgr/socket_utils_windows.c
  src/core/lib/iomgr/socket_windows.c
  src/core/lib/iomgr/tcp_client_posix.c
  src/core/lib/iomgr/tcp_client_uv.c
  src/core/lib/iomgr/tcp_client_windows.c
  src/core/lib/iomgr/tcp_posix.c
  src/core/lib/iomgr/tcp_server_posix.c
  src/core/lib/iomgr/tcp_server_uv.c
  src/core/lib/iomgr/tcp_server_windows.c
  src/core/lib/iomgr/tcp_uv.c
  src/core/lib/iomgr/tcp_windows.c
  src/core/lib/iomgr/time_averaged_stats.c
  src/core/lib/iomgr/timer_generic.c
  src/core/lib/iomgr/timer_heap.c
  src/core/lib/iomgr/timer_uv.c
  src/core/lib/iomgr/udp_server.c
  src/core/lib/iomgr/unix_sockets_posix.c
  src/core/lib/iomgr/unix_sockets_posix_noop.c
  src/core/lib/iomgr/wakeup_fd_cv.c
  src/core/lib/iomgr/wakeup_fd_eventfd.c
  src/core/lib/iomgr/wakeup_fd_nospecial.c
  src/core/lib/iomgr/wakeup_fd_pipe.c
  src/core/lib/iomgr/wakeup_fd_posix.c
  src/core/lib/iomgr/workqueue_uv.c
  src/core/lib/iomgr/workqueue_windows.c
  src/core/lib/json/json.c
  src/core/lib/json/json_reader.c
  src/core/lib/json/json_string.c
  src/core/lib/json/json_writer.c
  src/core/lib/slice/percent_encoding.c
  src/core/lib/slice/slice.c
  src/core/lib/slice/slice_buffer.c
  src/core/lib/slice/slice_string_helpers.c
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
  src/core/lib/transport/mdstr_hash_table.c
  src/core/lib/transport/metadata.c
  src/core/lib/transport/metadata_batch.c
  src/core/lib/transport/pid_controller.c
  src/core/lib/transport/service_config.c
  src/core/lib/transport/static_metadata.c
  src/core/lib/transport/timeout_encoding.c
  src/core/lib/transport/transport.c
  src/core/lib/transport/transport_op_string.c
  src/core/ext/transport/chttp2/alpn/alpn.c
  src/core/ext/client_channel/channel_connectivity.c
  src/core/ext/client_channel/client_channel.c
  src/core/ext/client_channel/client_channel_factory.c
  src/core/ext/client_channel/client_channel_plugin.c
  src/core/ext/client_channel/connector.c
  src/core/ext/client_channel/default_initial_connect_string.c
  src/core/ext/client_channel/http_connect_handshaker.c
  src/core/ext/client_channel/initial_connect_string.c
  src/core/ext/client_channel/lb_policy.c
  src/core/ext/client_channel/lb_policy_factory.c
  src/core/ext/client_channel/lb_policy_registry.c
  src/core/ext/client_channel/parse_address.c
  src/core/ext/client_channel/resolver.c
  src/core/ext/client_channel/resolver_factory.c
  src/core/ext/client_channel/resolver_registry.c
  src/core/ext/client_channel/subchannel.c
  src/core/ext/client_channel/subchannel_index.c
  src/core/ext/client_channel/uri_parser.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2.c
  src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c
  src/core/ext/transport/chttp2/server/chttp2_server.c
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
  third_party/nanopb/pb_common.c
  third_party/nanopb/pb_decode.c
  third_party/nanopb/pb_encode.c
)


target_include_directories(grpc++_cronet
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc++_cronet
  ${_gRPC_BASELIB_LIBRARIES}
  ${_gRPC_SSL_LIBRARIES}
  gpr
  grpc_cronet
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
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/grpc.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security_constants.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/census.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if (gRPC_INSTALL)
  install(TARGETS grpc++_cronet EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_library(grpc++_proto_reflection_desc_db
  test/cpp/util/proto_reflection_descriptor_database.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc++_proto_reflection_desc_db
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_proto_reflection_desc_db
  grpc++
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

endif (gRPC_BUILD_TESTS)

add_library(grpc++_reflection
  src/cpp/ext/proto_server_reflection.cc
  src/cpp/ext/proto_server_reflection_plugin.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc++_reflection
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_reflection
  grpc++
)

foreach(_hdr
  include/grpc++/ext/proto_server_reflection_plugin.h
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

if (gRPC_BUILD_TESTS)

add_library(grpc++_test
  src/cpp/test/server_context_test_spouse.cc
)


target_include_directories(grpc++_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(grpc++_test
  grpc++
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(grpc++_test_config
  test/cpp/util/test_config_cc.cc
)


target_include_directories(grpc++_test_config
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)



endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(grpc++_test_util
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h
  test/cpp/end2end/test_service_impl.cc
  test/cpp/util/byte_buffer_proto_helper.cc
  test/cpp/util/create_test_channel.cc
  test/cpp/util/string_ref_helper.cc
  test/cpp/util/subprocess.cc
  test/cpp/util/test_credentials_provider.cc
  src/cpp/codegen/codegen_init.cc
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
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc++_test_util
  grpc++
  grpc_test_util
)

foreach(_hdr
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
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc++/impl/codegen/thrift_serializer.h
  include/grpc++/impl/codegen/thrift_utils.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

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
  src/cpp/common/channel_arguments.cc
  src/cpp/common/channel_filter.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/core_codegen.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/dynamic_thread_pool.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/slice_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  src/cpp/codegen/codegen_init.cc
)


target_include_directories(grpc++_unsecure
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/exec_ctx_fwd.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_slice.h
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
  install(TARGETS grpc++_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

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

protobuf_generate_grpc_cpp(
  src/proto/grpc/reflection/v1alpha/reflection.proto
)

target_include_directories(grpc_cli_libs
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cli_libs
  grpc++_proto_reflection_desc_db
  grpc++
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


target_include_directories(grpc_plugin_support
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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

if (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http2_client_main
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  grpc++_test_config
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(interop_client_helper
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  test/cpp/interop/client_helper.cc
)

protobuf_generate_grpc_cpp(
  src/proto/grpc/testing/messages.proto
)

target_include_directories(interop_client_helper
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client_main
  interop_client_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(interop_server_helper
  test/cpp/interop/server_helper.cc
)


target_include_directories(interop_server_helper
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(interop_server_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server_lib
  interop_server_helper
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(interop_server_main
  test/cpp/interop/interop_server_bootstrap.cc
)


target_include_directories(interop_server_main
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(interop_server_main
  interop_server_lib
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

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
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/services.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/services.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/services.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/services.grpc.pb.h
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/driver.cc
  test/cpp/qps/parse_json.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/report.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
  test/cpp/util/benchmark_config.cc
)

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
  src/proto/grpc/testing/services.proto
)

target_include_directories(qps
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
  PRIVATE ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps
  grpc_test_util
  grpc++_test_util
  grpc++
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl
  src/boringssl/err_data.c
  third_party/boringssl/crypto/aes/aes.c
  third_party/boringssl/crypto/aes/mode_wrappers.c
  third_party/boringssl/crypto/asn1/a_bitstr.c
  third_party/boringssl/crypto/asn1/a_bool.c
  third_party/boringssl/crypto/asn1/a_d2i_fp.c
  third_party/boringssl/crypto/asn1/a_dup.c
  third_party/boringssl/crypto/asn1/a_enum.c
  third_party/boringssl/crypto/asn1/a_gentm.c
  third_party/boringssl/crypto/asn1/a_i2d_fp.c
  third_party/boringssl/crypto/asn1/a_int.c
  third_party/boringssl/crypto/asn1/a_mbstr.c
  third_party/boringssl/crypto/asn1/a_object.c
  third_party/boringssl/crypto/asn1/a_octet.c
  third_party/boringssl/crypto/asn1/a_print.c
  third_party/boringssl/crypto/asn1/a_strnid.c
  third_party/boringssl/crypto/asn1/a_time.c
  third_party/boringssl/crypto/asn1/a_type.c
  third_party/boringssl/crypto/asn1/a_utctm.c
  third_party/boringssl/crypto/asn1/a_utf8.c
  third_party/boringssl/crypto/asn1/asn1_lib.c
  third_party/boringssl/crypto/asn1/asn1_par.c
  third_party/boringssl/crypto/asn1/asn_pack.c
  third_party/boringssl/crypto/asn1/f_enum.c
  third_party/boringssl/crypto/asn1/f_int.c
  third_party/boringssl/crypto/asn1/f_string.c
  third_party/boringssl/crypto/asn1/t_bitst.c
  third_party/boringssl/crypto/asn1/tasn_dec.c
  third_party/boringssl/crypto/asn1/tasn_enc.c
  third_party/boringssl/crypto/asn1/tasn_fre.c
  third_party/boringssl/crypto/asn1/tasn_new.c
  third_party/boringssl/crypto/asn1/tasn_typ.c
  third_party/boringssl/crypto/asn1/tasn_utl.c
  third_party/boringssl/crypto/asn1/x_bignum.c
  third_party/boringssl/crypto/asn1/x_long.c
  third_party/boringssl/crypto/base64/base64.c
  third_party/boringssl/crypto/bio/bio.c
  third_party/boringssl/crypto/bio/bio_mem.c
  third_party/boringssl/crypto/bio/buffer.c
  third_party/boringssl/crypto/bio/connect.c
  third_party/boringssl/crypto/bio/fd.c
  third_party/boringssl/crypto/bio/file.c
  third_party/boringssl/crypto/bio/hexdump.c
  third_party/boringssl/crypto/bio/pair.c
  third_party/boringssl/crypto/bio/printf.c
  third_party/boringssl/crypto/bio/socket.c
  third_party/boringssl/crypto/bio/socket_helper.c
  third_party/boringssl/crypto/bn/add.c
  third_party/boringssl/crypto/bn/asm/x86_64-gcc.c
  third_party/boringssl/crypto/bn/bn.c
  third_party/boringssl/crypto/bn/bn_asn1.c
  third_party/boringssl/crypto/bn/cmp.c
  third_party/boringssl/crypto/bn/convert.c
  third_party/boringssl/crypto/bn/ctx.c
  third_party/boringssl/crypto/bn/div.c
  third_party/boringssl/crypto/bn/exponentiation.c
  third_party/boringssl/crypto/bn/gcd.c
  third_party/boringssl/crypto/bn/generic.c
  third_party/boringssl/crypto/bn/kronecker.c
  third_party/boringssl/crypto/bn/montgomery.c
  third_party/boringssl/crypto/bn/montgomery_inv.c
  third_party/boringssl/crypto/bn/mul.c
  third_party/boringssl/crypto/bn/prime.c
  third_party/boringssl/crypto/bn/random.c
  third_party/boringssl/crypto/bn/rsaz_exp.c
  third_party/boringssl/crypto/bn/shift.c
  third_party/boringssl/crypto/bn/sqrt.c
  third_party/boringssl/crypto/buf/buf.c
  third_party/boringssl/crypto/bytestring/asn1_compat.c
  third_party/boringssl/crypto/bytestring/ber.c
  third_party/boringssl/crypto/bytestring/cbb.c
  third_party/boringssl/crypto/bytestring/cbs.c
  third_party/boringssl/crypto/chacha/chacha.c
  third_party/boringssl/crypto/cipher/aead.c
  third_party/boringssl/crypto/cipher/cipher.c
  third_party/boringssl/crypto/cipher/derive_key.c
  third_party/boringssl/crypto/cipher/e_aes.c
  third_party/boringssl/crypto/cipher/e_chacha20poly1305.c
  third_party/boringssl/crypto/cipher/e_des.c
  third_party/boringssl/crypto/cipher/e_null.c
  third_party/boringssl/crypto/cipher/e_rc2.c
  third_party/boringssl/crypto/cipher/e_rc4.c
  third_party/boringssl/crypto/cipher/e_ssl3.c
  third_party/boringssl/crypto/cipher/e_tls.c
  third_party/boringssl/crypto/cipher/tls_cbc.c
  third_party/boringssl/crypto/cmac/cmac.c
  third_party/boringssl/crypto/conf/conf.c
  third_party/boringssl/crypto/cpu-aarch64-linux.c
  third_party/boringssl/crypto/cpu-arm-linux.c
  third_party/boringssl/crypto/cpu-arm.c
  third_party/boringssl/crypto/cpu-intel.c
  third_party/boringssl/crypto/cpu-ppc64le.c
  third_party/boringssl/crypto/crypto.c
  third_party/boringssl/crypto/curve25519/curve25519.c
  third_party/boringssl/crypto/curve25519/spake25519.c
  third_party/boringssl/crypto/curve25519/x25519-x86_64.c
  third_party/boringssl/crypto/des/des.c
  third_party/boringssl/crypto/dh/check.c
  third_party/boringssl/crypto/dh/dh.c
  third_party/boringssl/crypto/dh/dh_asn1.c
  third_party/boringssl/crypto/dh/params.c
  third_party/boringssl/crypto/digest/digest.c
  third_party/boringssl/crypto/digest/digests.c
  third_party/boringssl/crypto/dsa/dsa.c
  third_party/boringssl/crypto/dsa/dsa_asn1.c
  third_party/boringssl/crypto/ec/ec.c
  third_party/boringssl/crypto/ec/ec_asn1.c
  third_party/boringssl/crypto/ec/ec_key.c
  third_party/boringssl/crypto/ec/ec_montgomery.c
  third_party/boringssl/crypto/ec/oct.c
  third_party/boringssl/crypto/ec/p224-64.c
  third_party/boringssl/crypto/ec/p256-64.c
  third_party/boringssl/crypto/ec/p256-x86_64.c
  third_party/boringssl/crypto/ec/simple.c
  third_party/boringssl/crypto/ec/util-64.c
  third_party/boringssl/crypto/ec/wnaf.c
  third_party/boringssl/crypto/ecdh/ecdh.c
  third_party/boringssl/crypto/ecdsa/ecdsa.c
  third_party/boringssl/crypto/ecdsa/ecdsa_asn1.c
  third_party/boringssl/crypto/engine/engine.c
  third_party/boringssl/crypto/err/err.c
  third_party/boringssl/crypto/evp/digestsign.c
  third_party/boringssl/crypto/evp/evp.c
  third_party/boringssl/crypto/evp/evp_asn1.c
  third_party/boringssl/crypto/evp/evp_ctx.c
  third_party/boringssl/crypto/evp/p_dsa_asn1.c
  third_party/boringssl/crypto/evp/p_ec.c
  third_party/boringssl/crypto/evp/p_ec_asn1.c
  third_party/boringssl/crypto/evp/p_rsa.c
  third_party/boringssl/crypto/evp/p_rsa_asn1.c
  third_party/boringssl/crypto/evp/pbkdf.c
  third_party/boringssl/crypto/evp/print.c
  third_party/boringssl/crypto/evp/sign.c
  third_party/boringssl/crypto/ex_data.c
  third_party/boringssl/crypto/hkdf/hkdf.c
  third_party/boringssl/crypto/hmac/hmac.c
  third_party/boringssl/crypto/lhash/lhash.c
  third_party/boringssl/crypto/md4/md4.c
  third_party/boringssl/crypto/md5/md5.c
  third_party/boringssl/crypto/mem.c
  third_party/boringssl/crypto/modes/cbc.c
  third_party/boringssl/crypto/modes/cfb.c
  third_party/boringssl/crypto/modes/ctr.c
  third_party/boringssl/crypto/modes/gcm.c
  third_party/boringssl/crypto/modes/ofb.c
  third_party/boringssl/crypto/newhope/error_correction.c
  third_party/boringssl/crypto/newhope/newhope.c
  third_party/boringssl/crypto/newhope/ntt.c
  third_party/boringssl/crypto/newhope/poly.c
  third_party/boringssl/crypto/newhope/precomp.c
  third_party/boringssl/crypto/newhope/reduce.c
  third_party/boringssl/crypto/obj/obj.c
  third_party/boringssl/crypto/obj/obj_xref.c
  third_party/boringssl/crypto/pem/pem_all.c
  third_party/boringssl/crypto/pem/pem_info.c
  third_party/boringssl/crypto/pem/pem_lib.c
  third_party/boringssl/crypto/pem/pem_oth.c
  third_party/boringssl/crypto/pem/pem_pk8.c
  third_party/boringssl/crypto/pem/pem_pkey.c
  third_party/boringssl/crypto/pem/pem_x509.c
  third_party/boringssl/crypto/pem/pem_xaux.c
  third_party/boringssl/crypto/pkcs8/p5_pbe.c
  third_party/boringssl/crypto/pkcs8/p5_pbev2.c
  third_party/boringssl/crypto/pkcs8/p8_pkey.c
  third_party/boringssl/crypto/pkcs8/pkcs8.c
  third_party/boringssl/crypto/poly1305/poly1305.c
  third_party/boringssl/crypto/poly1305/poly1305_arm.c
  third_party/boringssl/crypto/poly1305/poly1305_vec.c
  third_party/boringssl/crypto/rand/deterministic.c
  third_party/boringssl/crypto/rand/rand.c
  third_party/boringssl/crypto/rand/urandom.c
  third_party/boringssl/crypto/rand/windows.c
  third_party/boringssl/crypto/rc4/rc4.c
  third_party/boringssl/crypto/refcount_c11.c
  third_party/boringssl/crypto/refcount_lock.c
  third_party/boringssl/crypto/rsa/blinding.c
  third_party/boringssl/crypto/rsa/padding.c
  third_party/boringssl/crypto/rsa/rsa.c
  third_party/boringssl/crypto/rsa/rsa_asn1.c
  third_party/boringssl/crypto/rsa/rsa_impl.c
  third_party/boringssl/crypto/sha/sha1.c
  third_party/boringssl/crypto/sha/sha256.c
  third_party/boringssl/crypto/sha/sha512.c
  third_party/boringssl/crypto/stack/stack.c
  third_party/boringssl/crypto/thread.c
  third_party/boringssl/crypto/thread_none.c
  third_party/boringssl/crypto/thread_pthread.c
  third_party/boringssl/crypto/thread_win.c
  third_party/boringssl/crypto/time_support.c
  third_party/boringssl/crypto/x509/a_digest.c
  third_party/boringssl/crypto/x509/a_sign.c
  third_party/boringssl/crypto/x509/a_strex.c
  third_party/boringssl/crypto/x509/a_verify.c
  third_party/boringssl/crypto/x509/algorithm.c
  third_party/boringssl/crypto/x509/asn1_gen.c
  third_party/boringssl/crypto/x509/by_dir.c
  third_party/boringssl/crypto/x509/by_file.c
  third_party/boringssl/crypto/x509/i2d_pr.c
  third_party/boringssl/crypto/x509/pkcs7.c
  third_party/boringssl/crypto/x509/rsa_pss.c
  third_party/boringssl/crypto/x509/t_crl.c
  third_party/boringssl/crypto/x509/t_req.c
  third_party/boringssl/crypto/x509/t_x509.c
  third_party/boringssl/crypto/x509/t_x509a.c
  third_party/boringssl/crypto/x509/x509.c
  third_party/boringssl/crypto/x509/x509_att.c
  third_party/boringssl/crypto/x509/x509_cmp.c
  third_party/boringssl/crypto/x509/x509_d2.c
  third_party/boringssl/crypto/x509/x509_def.c
  third_party/boringssl/crypto/x509/x509_ext.c
  third_party/boringssl/crypto/x509/x509_lu.c
  third_party/boringssl/crypto/x509/x509_obj.c
  third_party/boringssl/crypto/x509/x509_r2x.c
  third_party/boringssl/crypto/x509/x509_req.c
  third_party/boringssl/crypto/x509/x509_set.c
  third_party/boringssl/crypto/x509/x509_trs.c
  third_party/boringssl/crypto/x509/x509_txt.c
  third_party/boringssl/crypto/x509/x509_v3.c
  third_party/boringssl/crypto/x509/x509_vfy.c
  third_party/boringssl/crypto/x509/x509_vpm.c
  third_party/boringssl/crypto/x509/x509cset.c
  third_party/boringssl/crypto/x509/x509name.c
  third_party/boringssl/crypto/x509/x509rset.c
  third_party/boringssl/crypto/x509/x509spki.c
  third_party/boringssl/crypto/x509/x509type.c
  third_party/boringssl/crypto/x509/x_algor.c
  third_party/boringssl/crypto/x509/x_all.c
  third_party/boringssl/crypto/x509/x_attrib.c
  third_party/boringssl/crypto/x509/x_crl.c
  third_party/boringssl/crypto/x509/x_exten.c
  third_party/boringssl/crypto/x509/x_info.c
  third_party/boringssl/crypto/x509/x_name.c
  third_party/boringssl/crypto/x509/x_pkey.c
  third_party/boringssl/crypto/x509/x_pubkey.c
  third_party/boringssl/crypto/x509/x_req.c
  third_party/boringssl/crypto/x509/x_sig.c
  third_party/boringssl/crypto/x509/x_spki.c
  third_party/boringssl/crypto/x509/x_val.c
  third_party/boringssl/crypto/x509/x_x509.c
  third_party/boringssl/crypto/x509/x_x509a.c
  third_party/boringssl/crypto/x509v3/pcy_cache.c
  third_party/boringssl/crypto/x509v3/pcy_data.c
  third_party/boringssl/crypto/x509v3/pcy_lib.c
  third_party/boringssl/crypto/x509v3/pcy_map.c
  third_party/boringssl/crypto/x509v3/pcy_node.c
  third_party/boringssl/crypto/x509v3/pcy_tree.c
  third_party/boringssl/crypto/x509v3/v3_akey.c
  third_party/boringssl/crypto/x509v3/v3_akeya.c
  third_party/boringssl/crypto/x509v3/v3_alt.c
  third_party/boringssl/crypto/x509v3/v3_bcons.c
  third_party/boringssl/crypto/x509v3/v3_bitst.c
  third_party/boringssl/crypto/x509v3/v3_conf.c
  third_party/boringssl/crypto/x509v3/v3_cpols.c
  third_party/boringssl/crypto/x509v3/v3_crld.c
  third_party/boringssl/crypto/x509v3/v3_enum.c
  third_party/boringssl/crypto/x509v3/v3_extku.c
  third_party/boringssl/crypto/x509v3/v3_genn.c
  third_party/boringssl/crypto/x509v3/v3_ia5.c
  third_party/boringssl/crypto/x509v3/v3_info.c
  third_party/boringssl/crypto/x509v3/v3_int.c
  third_party/boringssl/crypto/x509v3/v3_lib.c
  third_party/boringssl/crypto/x509v3/v3_ncons.c
  third_party/boringssl/crypto/x509v3/v3_pci.c
  third_party/boringssl/crypto/x509v3/v3_pcia.c
  third_party/boringssl/crypto/x509v3/v3_pcons.c
  third_party/boringssl/crypto/x509v3/v3_pku.c
  third_party/boringssl/crypto/x509v3/v3_pmaps.c
  third_party/boringssl/crypto/x509v3/v3_prn.c
  third_party/boringssl/crypto/x509v3/v3_purp.c
  third_party/boringssl/crypto/x509v3/v3_skey.c
  third_party/boringssl/crypto/x509v3/v3_sxnet.c
  third_party/boringssl/crypto/x509v3/v3_utl.c
  third_party/boringssl/ssl/custom_extensions.c
  third_party/boringssl/ssl/d1_both.c
  third_party/boringssl/ssl/d1_lib.c
  third_party/boringssl/ssl/d1_pkt.c
  third_party/boringssl/ssl/d1_srtp.c
  third_party/boringssl/ssl/dtls_method.c
  third_party/boringssl/ssl/dtls_record.c
  third_party/boringssl/ssl/handshake_client.c
  third_party/boringssl/ssl/handshake_server.c
  third_party/boringssl/ssl/s3_both.c
  third_party/boringssl/ssl/s3_enc.c
  third_party/boringssl/ssl/s3_lib.c
  third_party/boringssl/ssl/s3_pkt.c
  third_party/boringssl/ssl/ssl_aead_ctx.c
  third_party/boringssl/ssl/ssl_asn1.c
  third_party/boringssl/ssl/ssl_buffer.c
  third_party/boringssl/ssl/ssl_cert.c
  third_party/boringssl/ssl/ssl_cipher.c
  third_party/boringssl/ssl/ssl_ecdh.c
  third_party/boringssl/ssl/ssl_file.c
  third_party/boringssl/ssl/ssl_lib.c
  third_party/boringssl/ssl/ssl_rsa.c
  third_party/boringssl/ssl/ssl_session.c
  third_party/boringssl/ssl/ssl_stat.c
  third_party/boringssl/ssl/t1_enc.c
  third_party/boringssl/ssl/t1_lib.c
  third_party/boringssl/ssl/tls13_both.c
  third_party/boringssl/ssl/tls13_client.c
  third_party/boringssl/ssl/tls13_enc.c
  third_party/boringssl/ssl/tls13_server.c
  third_party/boringssl/ssl/tls_method.c
  third_party/boringssl/ssl/tls_record.c
)


target_include_directories(boringssl
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl
  ${_gRPC_SSL_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_test_util
  third_party/boringssl/crypto/test/file_test.cc
  third_party/boringssl/crypto/test/malloc.cc
  third_party/boringssl/crypto/test/test_util.cc
)


target_include_directories(boringssl_test_util
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_test_util
  ${_gRPC_SSL_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_aes_test_lib
  third_party/boringssl/crypto/aes/aes_test.cc
)


target_include_directories(boringssl_aes_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_aes_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_asn1_test_lib
  third_party/boringssl/crypto/asn1/asn1_test.cc
)


target_include_directories(boringssl_asn1_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_asn1_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_base64_test_lib
  third_party/boringssl/crypto/base64/base64_test.cc
)


target_include_directories(boringssl_base64_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_base64_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_bio_test_lib
  third_party/boringssl/crypto/bio/bio_test.cc
)


target_include_directories(boringssl_bio_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bio_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_bn_test_lib
  third_party/boringssl/crypto/bn/bn_test.cc
)


target_include_directories(boringssl_bn_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bn_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_bytestring_test_lib
  third_party/boringssl/crypto/bytestring/bytestring_test.cc
)


target_include_directories(boringssl_bytestring_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bytestring_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_chacha_test_lib
  third_party/boringssl/crypto/chacha/chacha_test.cc
)


target_include_directories(boringssl_chacha_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_chacha_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_aead_test_lib
  third_party/boringssl/crypto/cipher/aead_test.cc
)


target_include_directories(boringssl_aead_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_aead_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_cipher_test_lib
  third_party/boringssl/crypto/cipher/cipher_test.cc
)


target_include_directories(boringssl_cipher_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_cipher_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_cmac_test_lib
  third_party/boringssl/crypto/cmac/cmac_test.cc
)


target_include_directories(boringssl_cmac_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_cmac_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_constant_time_test_lib
  third_party/boringssl/crypto/constant_time_test.c
)


target_include_directories(boringssl_constant_time_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_constant_time_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ed25519_test_lib
  third_party/boringssl/crypto/curve25519/ed25519_test.cc
)


target_include_directories(boringssl_ed25519_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ed25519_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_spake25519_test_lib
  third_party/boringssl/crypto/curve25519/spake25519_test.cc
)


target_include_directories(boringssl_spake25519_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_spake25519_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_x25519_test_lib
  third_party/boringssl/crypto/curve25519/x25519_test.cc
)


target_include_directories(boringssl_x25519_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_x25519_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_dh_test_lib
  third_party/boringssl/crypto/dh/dh_test.cc
)


target_include_directories(boringssl_dh_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_dh_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_digest_test_lib
  third_party/boringssl/crypto/digest/digest_test.cc
)


target_include_directories(boringssl_digest_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_digest_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_dsa_test_lib
  third_party/boringssl/crypto/dsa/dsa_test.c
)


target_include_directories(boringssl_dsa_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_dsa_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ec_test_lib
  third_party/boringssl/crypto/ec/ec_test.cc
)


target_include_directories(boringssl_ec_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ec_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_example_mul_lib
  third_party/boringssl/crypto/ec/example_mul.c
)


target_include_directories(boringssl_example_mul_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_example_mul_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ecdh_test_lib
  third_party/boringssl/crypto/ecdh/ecdh_test.cc
)


target_include_directories(boringssl_ecdh_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdh_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ecdsa_sign_test_lib
  third_party/boringssl/crypto/ecdsa/ecdsa_sign_test.cc
)


target_include_directories(boringssl_ecdsa_sign_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_sign_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ecdsa_test_lib
  third_party/boringssl/crypto/ecdsa/ecdsa_test.cc
)


target_include_directories(boringssl_ecdsa_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ecdsa_verify_test_lib
  third_party/boringssl/crypto/ecdsa/ecdsa_verify_test.cc
)


target_include_directories(boringssl_ecdsa_verify_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_verify_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_err_test_lib
  third_party/boringssl/crypto/err/err_test.cc
)


target_include_directories(boringssl_err_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_err_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_evp_extra_test_lib
  third_party/boringssl/crypto/evp/evp_extra_test.cc
)


target_include_directories(boringssl_evp_extra_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_evp_extra_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_evp_test_lib
  third_party/boringssl/crypto/evp/evp_test.cc
)


target_include_directories(boringssl_evp_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_evp_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_pbkdf_test_lib
  third_party/boringssl/crypto/evp/pbkdf_test.cc
)


target_include_directories(boringssl_pbkdf_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pbkdf_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_hkdf_test_lib
  third_party/boringssl/crypto/hkdf/hkdf_test.c
)


target_include_directories(boringssl_hkdf_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_hkdf_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_hmac_test_lib
  third_party/boringssl/crypto/hmac/hmac_test.cc
)


target_include_directories(boringssl_hmac_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_hmac_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_lhash_test_lib
  third_party/boringssl/crypto/lhash/lhash_test.c
)


target_include_directories(boringssl_lhash_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_lhash_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_gcm_test_lib
  third_party/boringssl/crypto/modes/gcm_test.cc
)


target_include_directories(boringssl_gcm_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_gcm_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_newhope_statistical_test_lib
  third_party/boringssl/crypto/newhope/newhope_statistical_test.cc
)


target_include_directories(boringssl_newhope_statistical_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_statistical_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_newhope_test_lib
  third_party/boringssl/crypto/newhope/newhope_test.cc
)


target_include_directories(boringssl_newhope_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_newhope_vectors_test_lib
  third_party/boringssl/crypto/newhope/newhope_vectors_test.cc
)


target_include_directories(boringssl_newhope_vectors_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_vectors_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_obj_test_lib
  third_party/boringssl/crypto/obj/obj_test.cc
)


target_include_directories(boringssl_obj_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_obj_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_pkcs12_test_lib
  third_party/boringssl/crypto/pkcs8/pkcs12_test.cc
)


target_include_directories(boringssl_pkcs12_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pkcs12_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_pkcs8_test_lib
  third_party/boringssl/crypto/pkcs8/pkcs8_test.cc
)


target_include_directories(boringssl_pkcs8_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pkcs8_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_poly1305_test_lib
  third_party/boringssl/crypto/poly1305/poly1305_test.cc
)


target_include_directories(boringssl_poly1305_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_poly1305_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_refcount_test_lib
  third_party/boringssl/crypto/refcount_test.c
)


target_include_directories(boringssl_refcount_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_refcount_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_rsa_test_lib
  third_party/boringssl/crypto/rsa/rsa_test.cc
)


target_include_directories(boringssl_rsa_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_rsa_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_thread_test_lib
  third_party/boringssl/crypto/thread_test.c
)


target_include_directories(boringssl_thread_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_thread_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_pkcs7_test_lib
  third_party/boringssl/crypto/x509/pkcs7_test.c
)


target_include_directories(boringssl_pkcs7_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_pkcs7_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_x509_test_lib
  third_party/boringssl/crypto/x509/x509_test.cc
)


target_include_directories(boringssl_x509_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_x509_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_tab_test_lib
  third_party/boringssl/crypto/x509v3/tab_test.c
)


target_include_directories(boringssl_tab_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_tab_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_v3name_test_lib
  third_party/boringssl/crypto/x509v3/v3name_test.c
)


target_include_directories(boringssl_v3name_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(boringssl_v3name_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(boringssl_ssl_test_lib
  third_party/boringssl/ssl/ssl_test.cc
)


target_include_directories(boringssl_ssl_test_lib
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ssl_test_lib
  ${_gRPC_SSL_LIBRARIES}
  boringssl_test_util
  boringssl
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(benchmark
  third_party/benchmark/src/benchmark.cc
  third_party/benchmark/src/benchmark_register.cc
  third_party/benchmark/src/colorprint.cc
  third_party/benchmark/src/commandlineflags.cc
  third_party/benchmark/src/complexity.cc
  third_party/benchmark/src/console_reporter.cc
  third_party/benchmark/src/csv_reporter.cc
  third_party/benchmark/src/json_reporter.cc
  third_party/benchmark/src/reporter.cc
  third_party/benchmark/src/sleep.cc
  third_party/benchmark/src/string_util.cc
  third_party/benchmark/src/sysinfo.cc
  third_party/benchmark/src/timers.cc
)


target_include_directories(benchmark
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(benchmark
  ${_gRPC_SSL_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(z
  third_party/zlib/adler32.c
  third_party/zlib/compress.c
  third_party/zlib/crc32.c
  third_party/zlib/deflate.c
  third_party/zlib/gzclose.c
  third_party/zlib/gzlib.c
  third_party/zlib/gzread.c
  third_party/zlib/gzwrite.c
  third_party/zlib/infback.c
  third_party/zlib/inffast.c
  third_party/zlib/inflate.c
  third_party/zlib/inftrees.c
  third_party/zlib/trees.c
  third_party/zlib/uncompr.c
  third_party/zlib/zutil.c
)


target_include_directories(z
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(z
  ${_gRPC_SSL_LIBRARIES}
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(bad_client_test
  test/core/bad_client/bad_client.c
)


target_include_directories(bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(bad_ssl_test_server
  test/core/bad_ssl/server_common.c
)


target_include_directories(bad_ssl_test_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bad_ssl_test_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(end2end_tests
  test/core/end2end/end2end_tests.c
  test/core/end2end/end2end_test_utils.c
  test/core/end2end/tests/authority_not_supported.c
  test/core/end2end/tests/bad_hostname.c
  test/core/end2end/tests/binary_metadata.c
  test/core/end2end/tests/call_creds.c
  test/core/end2end/tests/cancel_after_accept.c
  test/core/end2end/tests/cancel_after_client_done.c
  test/core/end2end/tests/cancel_after_invoke.c
  test/core/end2end/tests/cancel_before_invoke.c
  test/core/end2end/tests/cancel_in_a_vacuum.c
  test/core/end2end/tests/cancel_with_status.c
  test/core/end2end/tests/compressed_payload.c
  test/core/end2end/tests/connectivity.c
  test/core/end2end/tests/default_host.c
  test/core/end2end/tests/disappearing_server.c
  test/core/end2end/tests/empty_batch.c
  test/core/end2end/tests/filter_call_init_fails.c
  test/core/end2end/tests/filter_causes_close.c
  test/core/end2end/tests/filter_latency.c
  test/core/end2end/tests/graceful_server_shutdown.c
  test/core/end2end/tests/high_initial_seqno.c
  test/core/end2end/tests/hpack_size.c
  test/core/end2end/tests/idempotent_request.c
  test/core/end2end/tests/invoke_large_request.c
  test/core/end2end/tests/large_metadata.c
  test/core/end2end/tests/load_reporting_hook.c
  test/core/end2end/tests/max_concurrent_streams.c
  test/core/end2end/tests/max_message_length.c
  test/core/end2end/tests/negative_deadline.c
  test/core/end2end/tests/network_status_change.c
  test/core/end2end/tests/no_logging.c
  test/core/end2end/tests/no_op.c
  test/core/end2end/tests/payload.c
  test/core/end2end/tests/ping.c
  test/core/end2end/tests/ping_pong_streaming.c
  test/core/end2end/tests/registered_call.c
  test/core/end2end/tests/request_with_flags.c
  test/core/end2end/tests/request_with_payload.c
  test/core/end2end/tests/resource_quota_server.c
  test/core/end2end/tests/server_finishes_request.c
  test/core/end2end/tests/shutdown_finishes_calls.c
  test/core/end2end/tests/shutdown_finishes_tags.c
  test/core/end2end/tests/simple_cacheable_request.c
  test/core/end2end/tests/simple_delayed_request.c
  test/core/end2end/tests/simple_metadata.c
  test/core/end2end/tests/simple_request.c
  test/core/end2end/tests/streaming_error_response.c
  test/core/end2end/tests/trailing_metadata.c
  test/core/end2end/tests/write_buffering.c
  test/core/end2end/tests/write_buffering_at_end.c
)


target_include_directories(end2end_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(end2end_tests
  ${_gRPC_SSL_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_library(end2end_nosec_tests
  test/core/end2end/end2end_nosec_tests.c
  test/core/end2end/end2end_test_utils.c
  test/core/end2end/tests/authority_not_supported.c
  test/core/end2end/tests/bad_hostname.c
  test/core/end2end/tests/binary_metadata.c
  test/core/end2end/tests/cancel_after_accept.c
  test/core/end2end/tests/cancel_after_client_done.c
  test/core/end2end/tests/cancel_after_invoke.c
  test/core/end2end/tests/cancel_before_invoke.c
  test/core/end2end/tests/cancel_in_a_vacuum.c
  test/core/end2end/tests/cancel_with_status.c
  test/core/end2end/tests/compressed_payload.c
  test/core/end2end/tests/connectivity.c
  test/core/end2end/tests/default_host.c
  test/core/end2end/tests/disappearing_server.c
  test/core/end2end/tests/empty_batch.c
  test/core/end2end/tests/filter_call_init_fails.c
  test/core/end2end/tests/filter_causes_close.c
  test/core/end2end/tests/filter_latency.c
  test/core/end2end/tests/graceful_server_shutdown.c
  test/core/end2end/tests/high_initial_seqno.c
  test/core/end2end/tests/hpack_size.c
  test/core/end2end/tests/idempotent_request.c
  test/core/end2end/tests/invoke_large_request.c
  test/core/end2end/tests/large_metadata.c
  test/core/end2end/tests/load_reporting_hook.c
  test/core/end2end/tests/max_concurrent_streams.c
  test/core/end2end/tests/max_message_length.c
  test/core/end2end/tests/negative_deadline.c
  test/core/end2end/tests/network_status_change.c
  test/core/end2end/tests/no_logging.c
  test/core/end2end/tests/no_op.c
  test/core/end2end/tests/payload.c
  test/core/end2end/tests/ping.c
  test/core/end2end/tests/ping_pong_streaming.c
  test/core/end2end/tests/registered_call.c
  test/core/end2end/tests/request_with_flags.c
  test/core/end2end/tests/request_with_payload.c
  test/core/end2end/tests/resource_quota_server.c
  test/core/end2end/tests/server_finishes_request.c
  test/core/end2end/tests/shutdown_finishes_calls.c
  test/core/end2end/tests/shutdown_finishes_tags.c
  test/core/end2end/tests/simple_cacheable_request.c
  test/core/end2end/tests/simple_delayed_request.c
  test/core/end2end/tests/simple_metadata.c
  test/core/end2end/tests/simple_request.c
  test/core/end2end/tests/streaming_error_response.c
  test/core/end2end/tests/trailing_metadata.c
  test/core/end2end/tests/write_buffering.c
  test/core/end2end/tests/write_buffering_at_end.c
)


target_include_directories(end2end_nosec_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_INCLUDE_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)


endif (gRPC_BUILD_TESTS)

if (gRPC_BUILD_TESTS)

add_executable(alarm_test
  test/core/surface/alarm_test.c
)

target_include_directories(alarm_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(alarm_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(algorithm_test
  test/core/compression/algorithm_test.c
)

target_include_directories(algorithm_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(algorithm_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alloc_test
  test/core/support/alloc_test.c
)

target_include_directories(alloc_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(alloc_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alpn_test
  test/core/transport/chttp2/alpn_test.c
)

target_include_directories(alpn_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(alpn_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bad_server_response_test
  test/core/end2end/bad_server_response_test.c
)

target_include_directories(bad_server_response_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bad_server_response_test
  test_tcp_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bin_decoder_test
  test/core/transport/chttp2/bin_decoder_test.c
)

target_include_directories(bin_decoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bin_decoder_test
  grpc_test_util
  grpc
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bin_encoder_test
  test/core/transport/chttp2/bin_encoder_test.c
)

target_include_directories(bin_encoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bin_encoder_test
  grpc_test_util
  grpc
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(census_context_test
  test/core/census/context_test.c
)

target_include_directories(census_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(census_context_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(census_resource_test
  test/core/census/resource_test.c
)

target_include_directories(census_resource_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(census_resource_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(census_trace_context_test
  test/core/census/trace_context_test.c
)

target_include_directories(census_trace_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(census_trace_context_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_create_test
  test/core/surface/channel_create_test.c
)

target_include_directories(channel_create_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(channel_create_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_hpack_encoder_test
  test/core/transport/chttp2/hpack_encoder_test.c
)

target_include_directories(chttp2_hpack_encoder_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(chttp2_hpack_encoder_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_status_conversion_test
  test/core/transport/chttp2/status_conversion_test.c
)

target_include_directories(chttp2_status_conversion_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(chttp2_status_conversion_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_stream_map_test
  test/core/transport/chttp2/stream_map_test.c
)

target_include_directories(chttp2_stream_map_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(chttp2_stream_map_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(chttp2_varint_test
  test/core/transport/chttp2/varint_test.c
)

target_include_directories(chttp2_varint_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(chttp2_varint_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(combiner_test
  test/core/iomgr/combiner_test.c
)

target_include_directories(combiner_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(combiner_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(compression_test
  test/core/compression/compression_test.c
)

target_include_directories(compression_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(compression_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(concurrent_connectivity_test
  test/core/surface/concurrent_connectivity_test.c
)

target_include_directories(concurrent_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(concurrent_connectivity_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(connection_refused_test
  test/core/end2end/connection_refused_test.c
)

target_include_directories(connection_refused_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(connection_refused_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dns_resolver_connectivity_test
  test/core/client_channel/resolvers/dns_resolver_connectivity_test.c
)

target_include_directories(dns_resolver_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(dns_resolver_connectivity_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dns_resolver_test
  test/core/client_channel/resolvers/dns_resolver_test.c
)

target_include_directories(dns_resolver_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(dns_resolver_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(dualstack_socket_test
  test/core/end2end/dualstack_socket_test.c
)

target_include_directories(dualstack_socket_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(dualstack_socket_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(endpoint_pair_test
  test/core/iomgr/endpoint_pair_test.c
)

target_include_directories(endpoint_pair_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(endpoint_pair_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(ev_epoll_linux_test
  test/core/iomgr/ev_epoll_linux_test.c
)

target_include_directories(ev_epoll_linux_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(ev_epoll_linux_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fd_conservation_posix_test
  test/core/iomgr/fd_conservation_posix_test.c
)

target_include_directories(fd_conservation_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fd_conservation_posix_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fd_posix_test
  test/core/iomgr/fd_posix_test.c
)

target_include_directories(fd_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fd_posix_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_client
  test/core/fling/client.c
)

target_include_directories(fling_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fling_client
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_server
  test/core/fling/server.c
)

target_include_directories(fling_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fling_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_stream_test
  test/core/fling/fling_stream_test.c
)

target_include_directories(fling_stream_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fling_stream_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(fling_test
  test/core/fling/fling_test.c
)

target_include_directories(fling_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(fling_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)



if (gRPC_INSTALL)
  install(TARGETS gen_percent_encoding_tables EXPORT gRPCTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()

if (gRPC_BUILD_TESTS)

add_executable(goaway_server_test
  test/core/end2end/goaway_server_test.c
)

target_include_directories(goaway_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(goaway_server_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_avl_test
  test/core/support/avl_test.c
)

target_include_directories(gpr_avl_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_avl_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_backoff_test
  test/core/support/backoff_test.c
)

target_include_directories(gpr_backoff_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_backoff_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_cmdline_test
  test/core/support/cmdline_test.c
)

target_include_directories(gpr_cmdline_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_cmdline_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_cpu_test
  test/core/support/cpu_test.c
)

target_include_directories(gpr_cpu_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_cpu_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_env_test
  test/core/support/env_test.c
)

target_include_directories(gpr_env_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_env_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_histogram_test
  test/core/support/histogram_test.c
)

target_include_directories(gpr_histogram_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_histogram_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_host_port_test
  test/core/support/host_port_test.c
)

target_include_directories(gpr_host_port_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_host_port_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_log_test
  test/core/support/log_test.c
)

target_include_directories(gpr_log_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_log_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_mpscq_test
  test/core/support/mpscq_test.c
)

target_include_directories(gpr_mpscq_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_mpscq_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_stack_lockfree_test
  test/core/support/stack_lockfree_test.c
)

target_include_directories(gpr_stack_lockfree_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_stack_lockfree_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_string_test
  test/core/support/string_test.c
)

target_include_directories(gpr_string_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_string_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_sync_test
  test/core/support/sync_test.c
)

target_include_directories(gpr_sync_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_sync_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_thd_test
  test/core/support/thd_test.c
)

target_include_directories(gpr_thd_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_thd_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_time_test
  test/core/support/time_test.c
)

target_include_directories(gpr_time_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_time_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_tls_test
  test/core/support/tls_test.c
)

target_include_directories(gpr_tls_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_tls_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(gpr_useful_test
  test/core/support/useful_test.c
)

target_include_directories(gpr_useful_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(gpr_useful_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_auth_context_test
  test/core/security/auth_context_test.c
)

target_include_directories(grpc_auth_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_auth_context_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_b64_test
  test/core/security/b64_test.c
)

target_include_directories(grpc_b64_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_b64_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_byte_buffer_reader_test
  test/core/surface/byte_buffer_reader_test.c
)

target_include_directories(grpc_byte_buffer_reader_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_byte_buffer_reader_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_channel_args_test
  test/core/channel/channel_args_test.c
)

target_include_directories(grpc_channel_args_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_channel_args_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_channel_stack_test
  test/core/channel/channel_stack_test.c
)

target_include_directories(grpc_channel_stack_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_channel_stack_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_completion_queue_test
  test/core/surface/completion_queue_test.c
)

target_include_directories(grpc_completion_queue_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_completion_queue_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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

if (gRPC_BUILD_TESTS)

add_executable(grpc_credentials_test
  test/core/security/credentials_test.c
)

target_include_directories(grpc_credentials_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_credentials_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_fetch_oauth2
  test/core/security/fetch_oauth2.c
)

target_include_directories(grpc_fetch_oauth2
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_fetch_oauth2
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_invalid_channel_args_test
  test/core/surface/invalid_channel_args_test.c
)

target_include_directories(grpc_invalid_channel_args_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_invalid_channel_args_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_json_token_test
  test/core/security/json_token_test.c
)

target_include_directories(grpc_json_token_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_json_token_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_jwt_verifier_test
  test/core/security/jwt_verifier_test.c
)

target_include_directories(grpc_jwt_verifier_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_jwt_verifier_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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

if (gRPC_BUILD_TESTS)

add_executable(grpc_security_connector_test
  test/core/security/security_connector_test.c
)

target_include_directories(grpc_security_connector_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_security_connector_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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

if (gRPC_BUILD_TESTS)

add_executable(handshake_client
  test/core/handshake/client_ssl.c
)

target_include_directories(handshake_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(handshake_client
  ${_gRPC_SSL_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(handshake_server
  test/core/handshake/server_ssl.c
)

target_include_directories(handshake_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(handshake_server
  ${_gRPC_SSL_LIBRARIES}
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_parser_test
  test/core/transport/chttp2/hpack_parser_test.c
)

target_include_directories(hpack_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(hpack_parser_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_table_test
  test/core/transport/chttp2/hpack_table_test.c
)

target_include_directories(hpack_table_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(hpack_table_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_parser_test
  test/core/http/parser_test.c
)

target_include_directories(http_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(http_parser_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(httpcli_format_request_test
  test/core/http/format_request_test.c
)

target_include_directories(httpcli_format_request_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(httpcli_format_request_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(httpcli_test
  test/core/http/httpcli_test.c
)

target_include_directories(httpcli_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(httpcli_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(httpscli_test
  test/core/http/httpscli_test.c
)

target_include_directories(httpscli_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(httpscli_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(init_test
  test/core/surface/init_test.c
)

target_include_directories(init_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(init_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(internal_api_canary_iomgr_test
  test/core/internal_api_canaries/iomgr.c
)

target_include_directories(internal_api_canary_iomgr_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(internal_api_canary_iomgr_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(internal_api_canary_support_test
  test/core/internal_api_canaries/iomgr.c
)

target_include_directories(internal_api_canary_support_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(internal_api_canary_support_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(internal_api_canary_transport_test
  test/core/internal_api_canaries/iomgr.c
)

target_include_directories(internal_api_canary_transport_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(internal_api_canary_transport_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(invalid_call_argument_test
  test/core/end2end/invalid_call_argument_test.c
)

target_include_directories(invalid_call_argument_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(invalid_call_argument_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_rewrite
  test/core/json/json_rewrite.c
)

target_include_directories(json_rewrite
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(json_rewrite
  grpc
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_rewrite_test
  test/core/json/json_rewrite_test.c
)

target_include_directories(json_rewrite_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(json_rewrite_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_stream_error_test
  test/core/json/json_stream_error_test.c
)

target_include_directories(json_stream_error_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(json_stream_error_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_test
  test/core/json/json_test.c
)

target_include_directories(json_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(json_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(lame_client_test
  test/core/surface/lame_client_test.c
)

target_include_directories(lame_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(lame_client_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(lb_policies_test
  test/core/client_channel/lb_policies_test.c
)

target_include_directories(lb_policies_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(lb_policies_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(load_file_test
  test/core/iomgr/load_file_test.c
)

target_include_directories(load_file_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(load_file_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_profile_client
  test/core/memory_usage/client.c
)

target_include_directories(memory_profile_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(memory_profile_client
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_profile_server
  test/core/memory_usage/server.c
)

target_include_directories(memory_profile_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(memory_profile_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(memory_profile_test
  test/core/memory_usage/memory_usage_test.c
)

target_include_directories(memory_profile_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(memory_profile_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(message_compress_test
  test/core/compression/message_compress_test.c
)

target_include_directories(message_compress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(message_compress_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(mlog_test
  test/core/census/mlog_test.c
)

target_include_directories(mlog_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(mlog_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(multiple_server_queues_test
  test/core/end2end/multiple_server_queues_test.c
)

target_include_directories(multiple_server_queues_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(multiple_server_queues_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(murmur_hash_test
  test/core/support/murmur_hash_test.c
)

target_include_directories(murmur_hash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(murmur_hash_test
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(no_server_test
  test/core/end2end/no_server_test.c
)

target_include_directories(no_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(no_server_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_encoding_test
  test/core/slice/percent_encoding_test.c
)

target_include_directories(percent_encoding_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(percent_encoding_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(resolve_address_test
  test/core/iomgr/resolve_address_test.c
)

target_include_directories(resolve_address_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(resolve_address_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(resource_quota_test
  test/core/iomgr/resource_quota_test.c
)

target_include_directories(resource_quota_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(resource_quota_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(secure_channel_create_test
  test/core/surface/secure_channel_create_test.c
)

target_include_directories(secure_channel_create_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(secure_channel_create_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(secure_endpoint_test
  test/core/security/secure_endpoint_test.c
)

target_include_directories(secure_endpoint_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(secure_endpoint_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sequential_connectivity_test
  test/core/surface/sequential_connectivity_test.c
)

target_include_directories(sequential_connectivity_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(sequential_connectivity_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_chttp2_test
  test/core/surface/server_chttp2_test.c
)

target_include_directories(server_chttp2_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(server_chttp2_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_test
  test/core/surface/server_test.c
)

target_include_directories(server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(server_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(set_initial_connect_string_test
  test/core/client_channel/set_initial_connect_string_test.c
)

target_include_directories(set_initial_connect_string_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(set_initial_connect_string_test
  test_tcp_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_buffer_test
  test/core/slice/slice_buffer_test.c
)

target_include_directories(slice_buffer_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(slice_buffer_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_string_helpers_test
  test/core/slice/slice_string_helpers_test.c
)

target_include_directories(slice_string_helpers_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(slice_string_helpers_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(slice_test
  test/core/slice/slice_test.c
)

target_include_directories(slice_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(slice_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sockaddr_resolver_test
  test/core/client_channel/resolvers/sockaddr_resolver_test.c
)

target_include_directories(sockaddr_resolver_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(sockaddr_resolver_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(sockaddr_utils_test
  test/core/iomgr/sockaddr_utils_test.c
)

target_include_directories(sockaddr_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(sockaddr_utils_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(socket_utils_test
  test/core/iomgr/socket_utils_test.c
)

target_include_directories(socket_utils_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(socket_utils_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(tcp_client_posix_test
  test/core/iomgr/tcp_client_posix_test.c
)

target_include_directories(tcp_client_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(tcp_client_posix_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(tcp_posix_test
  test/core/iomgr/tcp_posix_test.c
)

target_include_directories(tcp_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(tcp_posix_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(tcp_server_posix_test
  test/core/iomgr/tcp_server_posix_test.c
)

target_include_directories(tcp_server_posix_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(tcp_server_posix_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(time_averaged_stats_test
  test/core/iomgr/time_averaged_stats_test.c
)

target_include_directories(time_averaged_stats_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(time_averaged_stats_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timeout_encoding_test
  test/core/transport/timeout_encoding_test.c
)

target_include_directories(timeout_encoding_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(timeout_encoding_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timer_heap_test
  test/core/iomgr/timer_heap_test.c
)

target_include_directories(timer_heap_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(timer_heap_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(timer_list_test
  test/core/iomgr/timer_list_test.c
)

target_include_directories(timer_list_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(timer_list_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_connectivity_state_test
  test/core/transport/connectivity_state_test.c
)

target_include_directories(transport_connectivity_state_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(transport_connectivity_state_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_metadata_test
  test/core/transport/metadata_test.c
)

target_include_directories(transport_metadata_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(transport_metadata_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_pid_controller_test
  test/core/transport/pid_controller_test.c
)

target_include_directories(transport_pid_controller_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(transport_pid_controller_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(transport_security_test
  test/core/tsi/transport_security_test.c
)

target_include_directories(transport_security_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(transport_security_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(udp_server_test
  test/core/iomgr/udp_server_test.c
)

target_include_directories(udp_server_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(udp_server_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(uri_parser_test
  test/core/client_channel/uri_parser_test.c
)

target_include_directories(uri_parser_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(uri_parser_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(wakeup_fd_cv_test
  test/core/iomgr/wakeup_fd_cv_test.c
)

target_include_directories(wakeup_fd_cv_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(wakeup_fd_cv_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(alarm_cpp_test
  test/cpp/common/alarm_cpp_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(alarm_cpp_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(alarm_cpp_test
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

add_executable(async_end2end_test
  test/cpp/end2end/async_end2end_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(async_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(async_end2end_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(auth_property_iterator_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(auth_property_iterator_test
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

add_executable(bm_fullstack
  test/cpp/microbenchmarks/bm_fullstack.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(bm_fullstack
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(bm_fullstack
  benchmark
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

add_executable(channel_arguments_test
  test/cpp/common/channel_arguments_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(channel_arguments_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(channel_arguments_test
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(channel_filter_test
  test/cpp/common/channel_filter_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(channel_filter_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(channel_filter_test
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cli_call_test
  test/cpp/util/cli_call_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(cli_call_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(cli_call_test
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

add_executable(client_crash_test
  test/cpp/end2end/client_crash_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(client_crash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(client_crash_test
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

add_executable(client_crash_test_server
  test/cpp/end2end/client_crash_test_server.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(client_crash_test_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(client_crash_test_server
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
  src/proto/grpc/testing/control.proto
  src/proto/grpc/testing/messages.proto
  src/proto/grpc/testing/payloads.proto
  src/proto/grpc/testing/services.proto
  src/proto/grpc/testing/stats.proto
  test/cpp/codegen/codegen_test_full.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(codegen_test_full
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(codegen_test_full
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(codegen_test_minimal
  src/proto/grpc/testing/control.proto
  src/proto/grpc/testing/messages.proto
  src/proto/grpc/testing/payloads.proto
  src/proto/grpc/testing/services.proto
  src/proto/grpc/testing/stats.proto
  test/cpp/codegen/codegen_test_minimal.cc
  src/cpp/codegen/codegen_init.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(codegen_test_minimal
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(codegen_test_minimal
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(credentials_test
  test/cpp/client/credentials_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(credentials_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(credentials_test
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_byte_buffer_test
  test/cpp/util/byte_buffer_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(cxx_byte_buffer_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(cxx_byte_buffer_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(cxx_slice_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(cxx_slice_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(cxx_string_ref_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(cxx_string_ref_test
  grpc++
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(cxx_time_test
  test/cpp/util/time_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(cxx_time_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(cxx_time_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(end2end_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(filter_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(filter_end2end_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(generic_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(generic_end2end_test
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
  src/proto/grpc/testing/compiler_test.proto
  test/cpp/codegen/golden_file_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(golden_file_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(golden_file_test
  grpc++
  grpc
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpc_cli
  test/cpp/util/grpc_cli.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(grpc_cli
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(grpc_cli
  grpc_cli_libs
  grpc++_proto_reflection_desc_db
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)

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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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


add_executable(grpc_php_plugin
  src/compiler/php_plugin.cc
)

target_include_directories(grpc_php_plugin
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(grpc_php_plugin
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc_plugin_support
)


if (gRPC_INSTALL)
  install(TARGETS grpc_php_plugin EXPORT gRPCTargets
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
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

if (gRPC_BUILD_TESTS)

add_executable(grpc_tool_test
  src/proto/grpc/testing/echo.proto
  src/proto/grpc/testing/echo_messages.proto
  test/cpp/util/grpc_tool_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(grpc_tool_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(grpc_tool_test
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
  src/proto/grpc/lb/v1/load_balancer.proto
  test/cpp/grpclb/grpclb_api_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(grpclb_api_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(grpclb_api_test
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(grpclb_test
  src/proto/grpc/lb/v1/load_balancer.proto
  test/cpp/grpclb/grpclb_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(grpclb_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(grpclb_test
  gpr
  gpr_test_util
  grpc
  grpc++
  grpc++_test_util
  grpc_test_util
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http2_client
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(http2_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(http2_client
  http2_client_main
  grpc++_test_util
  grpc_test_util
  grpc++
  grpc
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hybrid_end2end_test
  test/cpp/end2end/hybrid_end2end_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(hybrid_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(hybrid_end2end_test
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

add_executable(interop_client
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(interop_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(interop_client
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(interop_server
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(interop_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(interop_server
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(interop_test
  test/cpp/interop/interop_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(interop_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(interop_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_run_localhost
  test/cpp/qps/json_run_localhost.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(json_run_localhost
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(json_run_localhost
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

add_executable(metrics_client
  src/proto/grpc/testing/metrics.proto
  test/cpp/interop/metrics_client.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(metrics_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(metrics_client
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(mock_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(mock_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(noop-benchmark
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(noop-benchmark
  benchmark
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(proto_server_reflection_test
  test/cpp/end2end/proto_server_reflection_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(proto_server_reflection_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(proto_server_reflection_test
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

add_executable(qps_interarrival_test
  test/cpp/qps/qps_interarrival_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(qps_interarrival_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(qps_interarrival_test
  qps
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

add_executable(qps_json_driver
  test/cpp/qps/qps_json_driver.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(qps_json_driver
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(qps_json_driver
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(qps_openloop_test
  test/cpp/qps/qps_openloop_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(qps_openloop_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(qps_openloop_test
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(qps_worker
  test/cpp/qps/worker.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(qps_worker
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(qps_worker
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

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(reconnect_interop_client
  src/proto/grpc/testing/empty.proto
  src/proto/grpc/testing/messages.proto
  src/proto/grpc/testing/test.proto
  test/cpp/interop/reconnect_interop_client.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(reconnect_interop_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(reconnect_interop_client
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
  src/proto/grpc/testing/empty.proto
  src/proto/grpc/testing/messages.proto
  src/proto/grpc/testing/test.proto
  test/cpp/interop/reconnect_interop_server.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(reconnect_interop_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(reconnect_interop_server
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

add_executable(round_robin_end2end_test
  test/cpp/end2end/round_robin_end2end_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(round_robin_end2end_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(round_robin_end2end_test
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

add_executable(secure_auth_context_test
  test/cpp/common/secure_auth_context_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(secure_auth_context_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(secure_auth_context_test
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

add_executable(secure_sync_unary_ping_pong_test
  test/cpp/qps/secure_sync_unary_ping_pong_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(secure_sync_unary_ping_pong_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(secure_sync_unary_ping_pong_test
  qps
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

add_executable(server_builder_plugin_test
  test/cpp/end2end/server_builder_plugin_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(server_builder_plugin_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(server_builder_plugin_test
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

add_executable(server_context_test_spouse_test
  test/cpp/test/server_context_test_spouse_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(server_context_test_spouse_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(server_context_test_spouse_test
  grpc_test_util
  grpc++_test
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_crash_test
  test/cpp/end2end/server_crash_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(server_crash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(server_crash_test
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

add_executable(server_crash_test_client
  test/cpp/end2end/server_crash_test_client.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(server_crash_test_client
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(server_crash_test_client
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

add_executable(shutdown_test
  test/cpp/end2end/shutdown_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(shutdown_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(shutdown_test
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

add_executable(status_test
  test/cpp/util/status_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(status_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(status_test
  grpc_test_util
  grpc++
  grpc
  gpr_test_util
  gpr
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(streaming_throughput_test
  test/cpp/end2end/streaming_throughput_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(streaming_throughput_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(streaming_throughput_test
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

add_executable(stress_test
  src/proto/grpc/testing/empty.proto
  src/proto/grpc/testing/messages.proto
  src/proto/grpc/testing/metrics.proto
  src/proto/grpc/testing/test.proto
  test/cpp/interop/interop_client.cc
  test/cpp/interop/stress_interop_client.cc
  test/cpp/interop/stress_test.cc
  test/cpp/util/create_test_channel.cc
  test/cpp/util/metrics_server.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(stress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(stress_test
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
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(thread_manager_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(thread_manager_test
  grpc++
  grpc
  gpr
  grpc++_test_config
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(thread_stress_test
  test/cpp/end2end/thread_stress_test.cc
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(thread_stress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(thread_stress_test
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

add_executable(boringssl_aes_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_aes_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_aes_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_aes_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_asn1_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_asn1_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_asn1_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_asn1_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_base64_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_base64_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_base64_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_base64_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_bio_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_bio_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bio_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_bio_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_bn_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_bn_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bn_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_bn_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_bytestring_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_bytestring_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_bytestring_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_bytestring_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_chacha_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_chacha_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_chacha_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_chacha_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_aead_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_aead_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_aead_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_aead_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_cipher_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_cipher_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_cipher_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_cipher_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_cmac_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_cmac_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_cmac_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_cmac_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_constant_time_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_constant_time_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_constant_time_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_constant_time_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ed25519_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ed25519_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ed25519_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ed25519_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_spake25519_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_spake25519_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_spake25519_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_spake25519_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_x25519_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_x25519_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_x25519_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_x25519_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_dh_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_dh_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_dh_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_dh_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_digest_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_digest_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_digest_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_digest_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_dsa_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_dsa_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_dsa_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_dsa_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ec_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ec_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ec_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_example_mul
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_example_mul
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_example_mul
  ${_gRPC_SSL_LIBRARIES}
  boringssl_example_mul_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ecdh_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ecdh_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdh_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ecdh_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ecdsa_sign_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ecdsa_sign_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_sign_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ecdsa_sign_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ecdsa_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ecdsa_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ecdsa_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ecdsa_verify_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ecdsa_verify_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ecdsa_verify_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ecdsa_verify_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_err_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_err_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_err_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_err_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_evp_extra_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_evp_extra_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_evp_extra_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_evp_extra_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_evp_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_evp_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_evp_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_evp_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_pbkdf_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_pbkdf_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pbkdf_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_pbkdf_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_hkdf_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_hkdf_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_hkdf_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_hkdf_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_hmac_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_hmac_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_hmac_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_hmac_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_lhash_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_lhash_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_lhash_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_lhash_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_gcm_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_gcm_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_gcm_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_gcm_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_newhope_statistical_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_newhope_statistical_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_statistical_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_newhope_statistical_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_newhope_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_newhope_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_newhope_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_newhope_vectors_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_newhope_vectors_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_newhope_vectors_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_newhope_vectors_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_obj_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_obj_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_obj_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_obj_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_pkcs12_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_pkcs12_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pkcs12_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_pkcs12_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_pkcs8_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_pkcs8_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pkcs8_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_pkcs8_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_poly1305_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_poly1305_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_poly1305_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_poly1305_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_refcount_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_refcount_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_refcount_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_refcount_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_rsa_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_rsa_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_rsa_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_rsa_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_thread_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_thread_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_thread_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_thread_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_pkcs7_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_pkcs7_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_pkcs7_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_pkcs7_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_x509_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_x509_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_x509_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_x509_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_tab_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_tab_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_tab_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_tab_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_v3name_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_v3name_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_v3name_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_v3name_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(boringssl_ssl_test
  third_party/googletest/src/gtest-all.cc
)

target_include_directories(boringssl_ssl_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
  PRIVATE third_party/googletest/include
  PRIVATE third_party/googletest
)

target_link_libraries(boringssl_ssl_test
  ${_gRPC_SSL_LIBRARIES}
  boringssl_ssl_test_lib
  boringssl_test_util
  boringssl
  ${_gRPC_GFLAGS_LIBRARIES}
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(badreq_bad_client_test
  test/core/bad_client/tests/badreq.c
)

target_include_directories(badreq_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(badreq_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(connection_prefix_bad_client_test
  test/core/bad_client/tests/connection_prefix.c
)

target_include_directories(connection_prefix_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(connection_prefix_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(head_of_line_blocking_bad_client_test
  test/core/bad_client/tests/head_of_line_blocking.c
)

target_include_directories(head_of_line_blocking_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(head_of_line_blocking_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(headers_bad_client_test
  test/core/bad_client/tests/headers.c
)

target_include_directories(headers_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(headers_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(initial_settings_frame_bad_client_test
  test/core/bad_client/tests/initial_settings_frame.c
)

target_include_directories(initial_settings_frame_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(initial_settings_frame_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(large_metadata_bad_client_test
  test/core/bad_client/tests/large_metadata.c
)

target_include_directories(large_metadata_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(large_metadata_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_registered_method_bad_client_test
  test/core/bad_client/tests/server_registered_method.c
)

target_include_directories(server_registered_method_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(server_registered_method_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(simple_request_bad_client_test
  test/core/bad_client/tests/simple_request.c
)

target_include_directories(simple_request_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(simple_request_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(unknown_frame_bad_client_test
  test/core/bad_client/tests/unknown_frame.c
)

target_include_directories(unknown_frame_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(unknown_frame_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(window_overflow_bad_client_test
  test/core/bad_client/tests/window_overflow.c
)

target_include_directories(window_overflow_bad_client_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(window_overflow_bad_client_test
  ${_gRPC_SSL_LIBRARIES}
  bad_client_test
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bad_ssl_cert_server
  test/core/bad_ssl/servers/cert.c
)

target_include_directories(bad_ssl_cert_server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bad_ssl_cert_server
  bad_ssl_test_server
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(bad_ssl_cert_test
  test/core/bad_ssl/bad_ssl_test.c
)

target_include_directories(bad_ssl_cert_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(bad_ssl_cert_test
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_census_test
  test/core/end2end/fixtures/h2_census.c
)

target_include_directories(h2_census_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_census_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_compress_test
  test/core/end2end/fixtures/h2_compress.c
)

target_include_directories(h2_compress_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_compress_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_fakesec_test
  test/core/end2end/fixtures/h2_fakesec.c
)

target_include_directories(h2_fakesec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_fakesec_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_fd_test
  test/core/end2end/fixtures/h2_fd.c
)

target_include_directories(h2_fd_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_fd_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full_test
  test/core/end2end/fixtures/h2_full.c
)

target_include_directories(h2_full_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+pipe_test
  test/core/end2end/fixtures/h2_full+pipe.c
)

target_include_directories(h2_full+pipe_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full+pipe_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+trace_test
  test/core/end2end/fixtures/h2_full+trace.c
)

target_include_directories(h2_full+trace_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full+trace_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_http_proxy_test
  test/core/end2end/fixtures/h2_http_proxy.c
)

target_include_directories(h2_http_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_http_proxy_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_load_reporting_test
  test/core/end2end/fixtures/h2_load_reporting.c
)

target_include_directories(h2_load_reporting_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_load_reporting_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_oauth2_test
  test/core/end2end/fixtures/h2_oauth2.c
)

target_include_directories(h2_oauth2_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_oauth2_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_proxy_test
  test/core/end2end/fixtures/h2_proxy.c
)

target_include_directories(h2_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_proxy_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_test
  test/core/end2end/fixtures/h2_sockpair.c
)

target_include_directories(h2_sockpair_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair+trace_test
  test/core/end2end/fixtures/h2_sockpair+trace.c
)

target_include_directories(h2_sockpair+trace_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair+trace_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_1byte_test
  test/core/end2end/fixtures/h2_sockpair_1byte.c
)

target_include_directories(h2_sockpair_1byte_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair_1byte_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_test
  test/core/end2end/fixtures/h2_ssl.c
)

target_include_directories(h2_ssl_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_ssl_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_cert_test
  test/core/end2end/fixtures/h2_ssl_cert.c
)

target_include_directories(h2_ssl_cert_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_ssl_cert_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_ssl_proxy_test
  test/core/end2end/fixtures/h2_ssl_proxy.c
)

target_include_directories(h2_ssl_proxy_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_ssl_proxy_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_uds_test
  test/core/end2end/fixtures/h2_uds.c
)

target_include_directories(h2_uds_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_uds_test
  end2end_tests
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_census_nosec_test
  test/core/end2end/fixtures/h2_census.c
)

target_include_directories(h2_census_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_census_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_compress_nosec_test
  test/core/end2end/fixtures/h2_compress.c
)

target_include_directories(h2_compress_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_compress_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_fd_nosec_test
  test/core/end2end/fixtures/h2_fd.c
)

target_include_directories(h2_fd_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_fd_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full_nosec_test
  test/core/end2end/fixtures/h2_full.c
)

target_include_directories(h2_full_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+pipe_nosec_test
  test/core/end2end/fixtures/h2_full+pipe.c
)

target_include_directories(h2_full+pipe_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full+pipe_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_full+trace_nosec_test
  test/core/end2end/fixtures/h2_full+trace.c
)

target_include_directories(h2_full+trace_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_full+trace_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_http_proxy_nosec_test
  test/core/end2end/fixtures/h2_http_proxy.c
)

target_include_directories(h2_http_proxy_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_http_proxy_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_load_reporting_nosec_test
  test/core/end2end/fixtures/h2_load_reporting.c
)

target_include_directories(h2_load_reporting_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_load_reporting_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_proxy_nosec_test
  test/core/end2end/fixtures/h2_proxy.c
)

target_include_directories(h2_proxy_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_proxy_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_nosec_test
  test/core/end2end/fixtures/h2_sockpair.c
)

target_include_directories(h2_sockpair_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair+trace_nosec_test
  test/core/end2end/fixtures/h2_sockpair+trace.c
)

target_include_directories(h2_sockpair+trace_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair+trace_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_sockpair_1byte_nosec_test
  test/core/end2end/fixtures/h2_sockpair_1byte.c
)

target_include_directories(h2_sockpair_1byte_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_sockpair_1byte_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(h2_uds_nosec_test
  test/core/end2end/fixtures/h2_uds.c
)

target_include_directories(h2_uds_nosec_test
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(h2_uds_nosec_test
  end2end_nosec_tests
  grpc_test_util_unsecure
  grpc_unsecure
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(api_fuzzer_one_entry
  test/core/end2end/fuzzers/api_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(api_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(api_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(client_fuzzer_one_entry
  test/core/end2end/fuzzers/client_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(client_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(client_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(hpack_parser_fuzzer_test_one_entry
  test/core/transport/chttp2/hpack_parser_fuzzer_test.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(hpack_parser_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(hpack_parser_fuzzer_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_request_fuzzer_test_one_entry
  test/core/http/request_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(http_request_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(http_request_fuzzer_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(http_response_fuzzer_test_one_entry
  test/core/http/response_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(http_response_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(http_response_fuzzer_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(json_fuzzer_test_one_entry
  test/core/json/fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(json_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(json_fuzzer_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_response_test_one_entry
  test/core/nanopb/fuzzer_response.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(nanopb_fuzzer_response_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(nanopb_fuzzer_response_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(nanopb_fuzzer_serverlist_test_one_entry
  test/core/nanopb/fuzzer_serverlist.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(nanopb_fuzzer_serverlist_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(nanopb_fuzzer_serverlist_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_decode_fuzzer_one_entry
  test/core/slice/percent_decode_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(percent_decode_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(percent_decode_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(percent_encode_fuzzer_one_entry
  test/core/slice/percent_encode_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(percent_encode_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(percent_encode_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(server_fuzzer_one_entry
  test/core/end2end/fuzzers/server_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(server_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(server_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(ssl_server_fuzzer_one_entry
  test/core/security/ssl_server_fuzzer.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(ssl_server_fuzzer_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(ssl_server_fuzzer_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)
if (gRPC_BUILD_TESTS)

add_executable(uri_fuzzer_test_one_entry
  test/core/client_channel/uri_fuzzer_test.c
  test/core/util/one_corpus_entry_fuzzer.c
)

target_include_directories(uri_fuzzer_test_one_entry
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${BORINGSSL_ROOT_DIR}/include
  PRIVATE ${PROTOBUF_ROOT_DIR}/src
  PRIVATE ${ZLIB_ROOT_DIR}
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/third_party/gflags/include
)

target_link_libraries(uri_fuzzer_test_one_entry
  grpc_test_util
  grpc
  gpr_test_util
  gpr
)

endif (gRPC_BUILD_TESTS)







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
