# GRPC gyp file
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh

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

# Some of this file is built with the help of
# https://n8.io/converting-a-c-library-to-gyp/
{
  'target_defaults': {
    'default_configuration': 'Debug',
    'configurations': {
      'Debug': {
        'defines': [ 'DEBUG', '_DEBUG' ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 1, # static debug
          },
        },
      },
      'Release': {
        'defines': [ 'NDEBUG' ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 0, # static release
          },
        },
      }
    },
    'msvs_settings': {
      'VCLinkerTool': {
        'GenerateDebugInformation': 'true',
      },
    },
    'include_dirs': [
      'include'
    ],
    'libraries': [
      '-lcrypto',
      '-lssl',
      '-ldl',
      '-lpthread',
      '-lz',
      '-lprotobuf'
    ]
  },
  'targets': [
    {
      'target_name': 'gpr',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'src/core/support/alloc.c'
        'src/core/support/cmdline.c'
        'src/core/support/cpu_iphone.c'
        'src/core/support/cpu_linux.c'
        'src/core/support/cpu_posix.c'
        'src/core/support/cpu_windows.c'
        'src/core/support/env_linux.c'
        'src/core/support/env_posix.c'
        'src/core/support/env_win32.c'
        'src/core/support/file.c'
        'src/core/support/file_posix.c'
        'src/core/support/file_win32.c'
        'src/core/support/histogram.c'
        'src/core/support/host_port.c'
        'src/core/support/log.c'
        'src/core/support/log_android.c'
        'src/core/support/log_linux.c'
        'src/core/support/log_posix.c'
        'src/core/support/log_win32.c'
        'src/core/support/murmur_hash.c'
        'src/core/support/slice.c'
        'src/core/support/slice_buffer.c'
        'src/core/support/stack_lockfree.c'
        'src/core/support/string.c'
        'src/core/support/string_posix.c'
        'src/core/support/string_win32.c'
        'src/core/support/subprocess_posix.c'
        'src/core/support/sync.c'
        'src/core/support/sync_posix.c'
        'src/core/support/sync_win32.c'
        'src/core/support/thd.c'
        'src/core/support/thd_posix.c'
        'src/core/support/thd_win32.c'
        'src/core/support/time.c'
        'src/core/support/time_posix.c'
        'src/core/support/time_win32.c'
        'src/core/support/tls_pthread.c'
      ]
    },
    {
      'target_name': 'gpr_test_util',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'test/core/util/test_config.c'
      ]
    },
    {
      'target_name': 'grpc',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'src/core/httpcli/httpcli_security_connector.c'
        'src/core/security/base64.c'
        'src/core/security/client_auth_filter.c'
        'src/core/security/credentials.c'
        'src/core/security/credentials_metadata.c'
        'src/core/security/credentials_posix.c'
        'src/core/security/credentials_win32.c'
        'src/core/security/google_default_credentials.c'
        'src/core/security/json_token.c'
        'src/core/security/jwt_verifier.c'
        'src/core/security/secure_endpoint.c'
        'src/core/security/secure_transport_setup.c'
        'src/core/security/security_connector.c'
        'src/core/security/security_context.c'
        'src/core/security/server_auth_filter.c'
        'src/core/security/server_secure_chttp2.c'
        'src/core/surface/init_secure.c'
        'src/core/surface/secure_channel_create.c'
        'src/core/tsi/fake_transport_security.c'
        'src/core/tsi/ssl_transport_security.c'
        'src/core/tsi/transport_security.c'
        'src/core/census/grpc_context.c'
        'src/core/census/grpc_filter.c'
        'src/core/channel/channel_args.c'
        'src/core/channel/channel_stack.c'
        'src/core/channel/client_channel.c'
        'src/core/channel/compress_filter.c'
        'src/core/channel/connected_channel.c'
        'src/core/channel/http_client_filter.c'
        'src/core/channel/http_server_filter.c'
        'src/core/channel/noop_filter.c'
        'src/core/client_config/client_config.c'
        'src/core/client_config/connector.c'
        'src/core/client_config/lb_policies/pick_first.c'
        'src/core/client_config/lb_policies/round_robin.c'
        'src/core/client_config/lb_policy.c'
        'src/core/client_config/lb_policy_factory.c'
        'src/core/client_config/lb_policy_registry.c'
        'src/core/client_config/resolver.c'
        'src/core/client_config/resolver_factory.c'
        'src/core/client_config/resolver_registry.c'
        'src/core/client_config/resolvers/dns_resolver.c'
        'src/core/client_config/resolvers/sockaddr_resolver.c'
        'src/core/client_config/subchannel.c'
        'src/core/client_config/subchannel_factory.c'
        'src/core/client_config/subchannel_factory_decorators/add_channel_arg.c'
        'src/core/client_config/subchannel_factory_decorators/merge_channel_args.c'
        'src/core/client_config/uri_parser.c'
        'src/core/compression/algorithm.c'
        'src/core/compression/message_compress.c'
        'src/core/debug/trace.c'
        'src/core/httpcli/format_request.c'
        'src/core/httpcli/httpcli.c'
        'src/core/httpcli/parser.c'
        'src/core/iomgr/alarm.c'
        'src/core/iomgr/alarm_heap.c'
        'src/core/iomgr/endpoint.c'
        'src/core/iomgr/endpoint_pair_posix.c'
        'src/core/iomgr/endpoint_pair_windows.c'
        'src/core/iomgr/fd_posix.c'
        'src/core/iomgr/iocp_windows.c'
        'src/core/iomgr/iomgr.c'
        'src/core/iomgr/iomgr_posix.c'
        'src/core/iomgr/iomgr_windows.c'
        'src/core/iomgr/pollset_multipoller_with_epoll.c'
        'src/core/iomgr/pollset_multipoller_with_poll_posix.c'
        'src/core/iomgr/pollset_posix.c'
        'src/core/iomgr/pollset_set_posix.c'
        'src/core/iomgr/pollset_set_windows.c'
        'src/core/iomgr/pollset_windows.c'
        'src/core/iomgr/resolve_address_posix.c'
        'src/core/iomgr/resolve_address_windows.c'
        'src/core/iomgr/sockaddr_utils.c'
        'src/core/iomgr/socket_utils_common_posix.c'
        'src/core/iomgr/socket_utils_linux.c'
        'src/core/iomgr/socket_utils_posix.c'
        'src/core/iomgr/socket_windows.c'
        'src/core/iomgr/tcp_client_posix.c'
        'src/core/iomgr/tcp_client_windows.c'
        'src/core/iomgr/tcp_posix.c'
        'src/core/iomgr/tcp_server_posix.c'
        'src/core/iomgr/tcp_server_windows.c'
        'src/core/iomgr/tcp_windows.c'
        'src/core/iomgr/time_averaged_stats.c'
        'src/core/iomgr/udp_server.c'
        'src/core/iomgr/wakeup_fd_eventfd.c'
        'src/core/iomgr/wakeup_fd_nospecial.c'
        'src/core/iomgr/wakeup_fd_pipe.c'
        'src/core/iomgr/wakeup_fd_posix.c'
        'src/core/json/json.c'
        'src/core/json/json_reader.c'
        'src/core/json/json_string.c'
        'src/core/json/json_writer.c'
        'src/core/profiling/basic_timers.c'
        'src/core/profiling/stap_timers.c'
        'src/core/surface/byte_buffer.c'
        'src/core/surface/byte_buffer_queue.c'
        'src/core/surface/byte_buffer_reader.c'
        'src/core/surface/call.c'
        'src/core/surface/call_details.c'
        'src/core/surface/call_log_batch.c'
        'src/core/surface/channel.c'
        'src/core/surface/channel_connectivity.c'
        'src/core/surface/channel_create.c'
        'src/core/surface/completion_queue.c'
        'src/core/surface/event_string.c'
        'src/core/surface/init.c'
        'src/core/surface/lame_client.c'
        'src/core/surface/metadata_array.c'
        'src/core/surface/server.c'
        'src/core/surface/server_chttp2.c'
        'src/core/surface/server_create.c'
        'src/core/surface/surface_trace.c'
        'src/core/surface/version.c'
        'src/core/transport/chttp2/alpn.c'
        'src/core/transport/chttp2/bin_encoder.c'
        'src/core/transport/chttp2/frame_data.c'
        'src/core/transport/chttp2/frame_goaway.c'
        'src/core/transport/chttp2/frame_ping.c'
        'src/core/transport/chttp2/frame_rst_stream.c'
        'src/core/transport/chttp2/frame_settings.c'
        'src/core/transport/chttp2/frame_window_update.c'
        'src/core/transport/chttp2/hpack_parser.c'
        'src/core/transport/chttp2/hpack_table.c'
        'src/core/transport/chttp2/huffsyms.c'
        'src/core/transport/chttp2/incoming_metadata.c'
        'src/core/transport/chttp2/parsing.c'
        'src/core/transport/chttp2/status_conversion.c'
        'src/core/transport/chttp2/stream_encoder.c'
        'src/core/transport/chttp2/stream_lists.c'
        'src/core/transport/chttp2/stream_map.c'
        'src/core/transport/chttp2/timeout_encoding.c'
        'src/core/transport/chttp2/varint.c'
        'src/core/transport/chttp2/writing.c'
        'src/core/transport/chttp2_transport.c'
        'src/core/transport/connectivity_state.c'
        'src/core/transport/metadata.c'
        'src/core/transport/stream_op.c'
        'src/core/transport/transport.c'
        'src/core/transport/transport_op_string.c'
        'src/core/census/context.c'
        'src/core/census/initialize.c'
        'src/core/census/operation.c'
        'src/core/census/tracing.c'
      ]
    },
    {
      'target_name': 'grpc_test_util',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'gpr_test_util',
        'grpc',
      ],
      'sources': [
        'test/core/end2end/data/server1_cert.c'
        'test/core/end2end/data/server1_key.c'
        'test/core/end2end/data/test_root_cert.c'
        'test/core/end2end/cq_verifier.c'
        'test/core/end2end/fixtures/proxy.c'
        'test/core/iomgr/endpoint_tests.c'
        'test/core/security/oauth2_utils.c'
        'test/core/util/grpc_profiler.c'
        'test/core/util/parse_hexstring.c'
        'test/core/util/port_posix.c'
        'test/core/util/port_windows.c'
        'test/core/util/slice_splitter.c'
      ]
    },
    {
      'target_name': 'grpc_test_util_unsecure',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'gpr_test_util',
        'grpc',
      ],
      'sources': [
        'test/core/end2end/cq_verifier.c'
        'test/core/end2end/fixtures/proxy.c'
        'test/core/iomgr/endpoint_tests.c'
        'test/core/security/oauth2_utils.c'
        'test/core/util/grpc_profiler.c'
        'test/core/util/parse_hexstring.c'
        'test/core/util/port_posix.c'
        'test/core/util/port_windows.c'
        'test/core/util/slice_splitter.c'
      ]
    },
    {
      'target_name': 'grpc_unsecure',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
      ],
      'sources': [
        'src/core/surface/init_unsecure.c'
        'src/core/census/grpc_context.c'
        'src/core/census/grpc_filter.c'
        'src/core/channel/channel_args.c'
        'src/core/channel/channel_stack.c'
        'src/core/channel/client_channel.c'
        'src/core/channel/compress_filter.c'
        'src/core/channel/connected_channel.c'
        'src/core/channel/http_client_filter.c'
        'src/core/channel/http_server_filter.c'
        'src/core/channel/noop_filter.c'
        'src/core/client_config/client_config.c'
        'src/core/client_config/connector.c'
        'src/core/client_config/lb_policies/pick_first.c'
        'src/core/client_config/lb_policies/round_robin.c'
        'src/core/client_config/lb_policy.c'
        'src/core/client_config/lb_policy_factory.c'
        'src/core/client_config/lb_policy_registry.c'
        'src/core/client_config/resolver.c'
        'src/core/client_config/resolver_factory.c'
        'src/core/client_config/resolver_registry.c'
        'src/core/client_config/resolvers/dns_resolver.c'
        'src/core/client_config/resolvers/sockaddr_resolver.c'
        'src/core/client_config/subchannel.c'
        'src/core/client_config/subchannel_factory.c'
        'src/core/client_config/subchannel_factory_decorators/add_channel_arg.c'
        'src/core/client_config/subchannel_factory_decorators/merge_channel_args.c'
        'src/core/client_config/uri_parser.c'
        'src/core/compression/algorithm.c'
        'src/core/compression/message_compress.c'
        'src/core/debug/trace.c'
        'src/core/httpcli/format_request.c'
        'src/core/httpcli/httpcli.c'
        'src/core/httpcli/parser.c'
        'src/core/iomgr/alarm.c'
        'src/core/iomgr/alarm_heap.c'
        'src/core/iomgr/endpoint.c'
        'src/core/iomgr/endpoint_pair_posix.c'
        'src/core/iomgr/endpoint_pair_windows.c'
        'src/core/iomgr/fd_posix.c'
        'src/core/iomgr/iocp_windows.c'
        'src/core/iomgr/iomgr.c'
        'src/core/iomgr/iomgr_posix.c'
        'src/core/iomgr/iomgr_windows.c'
        'src/core/iomgr/pollset_multipoller_with_epoll.c'
        'src/core/iomgr/pollset_multipoller_with_poll_posix.c'
        'src/core/iomgr/pollset_posix.c'
        'src/core/iomgr/pollset_set_posix.c'
        'src/core/iomgr/pollset_set_windows.c'
        'src/core/iomgr/pollset_windows.c'
        'src/core/iomgr/resolve_address_posix.c'
        'src/core/iomgr/resolve_address_windows.c'
        'src/core/iomgr/sockaddr_utils.c'
        'src/core/iomgr/socket_utils_common_posix.c'
        'src/core/iomgr/socket_utils_linux.c'
        'src/core/iomgr/socket_utils_posix.c'
        'src/core/iomgr/socket_windows.c'
        'src/core/iomgr/tcp_client_posix.c'
        'src/core/iomgr/tcp_client_windows.c'
        'src/core/iomgr/tcp_posix.c'
        'src/core/iomgr/tcp_server_posix.c'
        'src/core/iomgr/tcp_server_windows.c'
        'src/core/iomgr/tcp_windows.c'
        'src/core/iomgr/time_averaged_stats.c'
        'src/core/iomgr/udp_server.c'
        'src/core/iomgr/wakeup_fd_eventfd.c'
        'src/core/iomgr/wakeup_fd_nospecial.c'
        'src/core/iomgr/wakeup_fd_pipe.c'
        'src/core/iomgr/wakeup_fd_posix.c'
        'src/core/json/json.c'
        'src/core/json/json_reader.c'
        'src/core/json/json_string.c'
        'src/core/json/json_writer.c'
        'src/core/profiling/basic_timers.c'
        'src/core/profiling/stap_timers.c'
        'src/core/surface/byte_buffer.c'
        'src/core/surface/byte_buffer_queue.c'
        'src/core/surface/byte_buffer_reader.c'
        'src/core/surface/call.c'
        'src/core/surface/call_details.c'
        'src/core/surface/call_log_batch.c'
        'src/core/surface/channel.c'
        'src/core/surface/channel_connectivity.c'
        'src/core/surface/channel_create.c'
        'src/core/surface/completion_queue.c'
        'src/core/surface/event_string.c'
        'src/core/surface/init.c'
        'src/core/surface/lame_client.c'
        'src/core/surface/metadata_array.c'
        'src/core/surface/server.c'
        'src/core/surface/server_chttp2.c'
        'src/core/surface/server_create.c'
        'src/core/surface/surface_trace.c'
        'src/core/surface/version.c'
        'src/core/transport/chttp2/alpn.c'
        'src/core/transport/chttp2/bin_encoder.c'
        'src/core/transport/chttp2/frame_data.c'
        'src/core/transport/chttp2/frame_goaway.c'
        'src/core/transport/chttp2/frame_ping.c'
        'src/core/transport/chttp2/frame_rst_stream.c'
        'src/core/transport/chttp2/frame_settings.c'
        'src/core/transport/chttp2/frame_window_update.c'
        'src/core/transport/chttp2/hpack_parser.c'
        'src/core/transport/chttp2/hpack_table.c'
        'src/core/transport/chttp2/huffsyms.c'
        'src/core/transport/chttp2/incoming_metadata.c'
        'src/core/transport/chttp2/parsing.c'
        'src/core/transport/chttp2/status_conversion.c'
        'src/core/transport/chttp2/stream_encoder.c'
        'src/core/transport/chttp2/stream_lists.c'
        'src/core/transport/chttp2/stream_map.c'
        'src/core/transport/chttp2/timeout_encoding.c'
        'src/core/transport/chttp2/varint.c'
        'src/core/transport/chttp2/writing.c'
        'src/core/transport/chttp2_transport.c'
        'src/core/transport/connectivity_state.c'
        'src/core/transport/metadata.c'
        'src/core/transport/stream_op.c'
        'src/core/transport/transport.c'
        'src/core/transport/transport_op_string.c'
        'src/core/census/context.c'
        'src/core/census/initialize.c'
        'src/core/census/operation.c'
        'src/core/census/tracing.c'
      ]
    },
    {
      'target_name': 'grpc_zookeeper',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'grpc',
      ],
      'sources': [
        'src/core/client_config/resolvers/zookeeper_resolver.c'
      ]
    },
    {
      'target_name': 'reconnect_server',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/util/reconnect_server.c'
      ]
    },
    {
      'target_name': 'grpc++',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'grpc',
      ],
      'sources': [
        'src/cpp/client/secure_channel_arguments.cc'
        'src/cpp/client/secure_credentials.cc'
        'src/cpp/common/auth_property_iterator.cc'
        'src/cpp/common/secure_auth_context.cc'
        'src/cpp/common/secure_create_auth_context.cc'
        'src/cpp/server/secure_server_credentials.cc'
        'src/cpp/client/channel.cc'
        'src/cpp/client/channel_arguments.cc'
        'src/cpp/client/client_context.cc'
        'src/cpp/client/create_channel.cc'
        'src/cpp/client/create_channel_internal.cc'
        'src/cpp/client/credentials.cc'
        'src/cpp/client/generic_stub.cc'
        'src/cpp/client/insecure_credentials.cc'
        'src/cpp/common/call.cc'
        'src/cpp/common/completion_queue.cc'
        'src/cpp/common/rpc_method.cc'
        'src/cpp/proto/proto_utils.cc'
        'src/cpp/server/async_generic_service.cc'
        'src/cpp/server/create_default_thread_pool.cc'
        'src/cpp/server/dynamic_thread_pool.cc'
        'src/cpp/server/fixed_size_thread_pool.cc'
        'src/cpp/server/insecure_server_credentials.cc'
        'src/cpp/server/server.cc'
        'src/cpp/server/server_builder.cc'
        'src/cpp/server/server_context.cc'
        'src/cpp/server/server_credentials.cc'
        'src/cpp/util/byte_buffer.cc'
        'src/cpp/util/slice.cc'
        'src/cpp/util/status.cc'
        'src/cpp/util/string_ref.cc'
        'src/cpp/util/time.cc'
      ]
    },
    {
      'target_name': 'grpc++_test_config',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'test/cpp/util/test_config.cc'
      ]
    },
    {
      'target_name': 'grpc++_test_util',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc++',
        'grpc_test_util',
      ],
      'sources': [
        'test/cpp/util/messages.proto'
        'test/cpp/util/echo.proto'
        'test/cpp/util/echo_duplicate.proto'
        'test/cpp/util/cli_call.cc'
        'test/cpp/util/create_test_channel.cc'
        'test/cpp/util/string_ref_helper.cc'
        'test/cpp/util/subprocess.cc'
      ]
    },
    {
      'target_name': 'grpc++_unsecure',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'grpc_unsecure',
      ],
      'sources': [
        'src/cpp/common/insecure_create_auth_context.cc'
        'src/cpp/client/channel.cc'
        'src/cpp/client/channel_arguments.cc'
        'src/cpp/client/client_context.cc'
        'src/cpp/client/create_channel.cc'
        'src/cpp/client/create_channel_internal.cc'
        'src/cpp/client/credentials.cc'
        'src/cpp/client/generic_stub.cc'
        'src/cpp/client/insecure_credentials.cc'
        'src/cpp/common/call.cc'
        'src/cpp/common/completion_queue.cc'
        'src/cpp/common/rpc_method.cc'
        'src/cpp/proto/proto_utils.cc'
        'src/cpp/server/async_generic_service.cc'
        'src/cpp/server/create_default_thread_pool.cc'
        'src/cpp/server/dynamic_thread_pool.cc'
        'src/cpp/server/fixed_size_thread_pool.cc'
        'src/cpp/server/insecure_server_credentials.cc'
        'src/cpp/server/server.cc'
        'src/cpp/server/server_builder.cc'
        'src/cpp/server/server_context.cc'
        'src/cpp/server/server_credentials.cc'
        'src/cpp/util/byte_buffer.cc'
        'src/cpp/util/slice.cc'
        'src/cpp/util/status.cc'
        'src/cpp/util/string_ref.cc'
        'src/cpp/util/time.cc'
      ]
    },
    {
      'target_name': 'grpc_plugin_support',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'src/compiler/cpp_generator.cc'
        'src/compiler/csharp_generator.cc'
        'src/compiler/objective_c_generator.cc'
        'src/compiler/python_generator.cc'
        'src/compiler/ruby_generator.cc'
      ]
    },
    {
      'target_name': 'interop_client_helper',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr',
      ],
      'sources': [
        'test/proto/messages.proto'
        'test/cpp/interop/client_helper.cc'
      ]
    },
    {
      'target_name': 'interop_client_main',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'interop_client_helper',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/proto/empty.proto'
        'test/proto/messages.proto'
        'test/proto/test.proto'
        'test/cpp/interop/client.cc'
        'test/cpp/interop/interop_client.cc'
      ]
    },
    {
      'target_name': 'interop_server_helper',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr',
      ],
      'sources': [
        'test/cpp/interop/server_helper.cc'
      ]
    },
    {
      'target_name': 'interop_server_main',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'interop_server_helper',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/proto/empty.proto'
        'test/proto/messages.proto'
        'test/proto/test.proto'
        'test/cpp/interop/server.cc'
      ]
    },
    {
      'target_name': 'qps',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util',
        'grpc++_test_util',
        'grpc++',
      ],
      'sources': [
        'test/cpp/qps/qpstest.proto'
        'test/cpp/qps/perf_db.proto'
        'test/cpp/qps/client_async.cc'
        'test/cpp/qps/client_sync.cc'
        'test/cpp/qps/driver.cc'
        'test/cpp/qps/perf_db_client.cc'
        'test/cpp/qps/qps_worker.cc'
        'test/cpp/qps/report.cc'
        'test/cpp/qps/server_async.cc'
        'test/cpp/qps/server_sync.cc'
        'test/cpp/qps/timer.cc'
        'test/cpp/util/benchmark_config.cc'
      ]
    },
    {
      'target_name': 'grpc_csharp_ext',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'gpr',
        'grpc',
      ],
      'sources': [
        'src/csharp/ext/grpc_csharp_ext.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_compress',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_compress.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_fakesec',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_fakesec.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_full',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_full.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_full+poll',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_full+poll.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_oauth2',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_oauth2.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_proxy',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_proxy.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_sockpair',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_sockpair.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_sockpair+trace',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_sockpair+trace.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_sockpair_1byte',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_sockpair_1byte.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_ssl',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_ssl.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_ssl+poll',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_ssl+poll.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_ssl_proxy',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_ssl_proxy.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_uds',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_uds.c'
      ]
    },
    {
      'target_name': 'end2end_fixture_h2_uds+poll',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/fixtures/h2_uds+poll.c'
      ]
    },
    {
      'target_name': 'end2end_test_bad_hostname',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/bad_hostname.c'
      ]
    },
    {
      'target_name': 'end2end_test_binary_metadata',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/binary_metadata.c'
      ]
    },
    {
      'target_name': 'end2end_test_call_creds',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/call_creds.c'
      ]
    },
    {
      'target_name': 'end2end_test_cancel_after_accept',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/cancel_after_accept.c'
      ]
    },
    {
      'target_name': 'end2end_test_cancel_after_client_done',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/cancel_after_client_done.c'
      ]
    },
    {
      'target_name': 'end2end_test_cancel_after_invoke',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/cancel_after_invoke.c'
      ]
    },
    {
      'target_name': 'end2end_test_cancel_before_invoke',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/cancel_before_invoke.c'
      ]
    },
    {
      'target_name': 'end2end_test_cancel_in_a_vacuum',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/cancel_in_a_vacuum.c'
      ]
    },
    {
      'target_name': 'end2end_test_census_simple_request',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/census_simple_request.c'
      ]
    },
    {
      'target_name': 'end2end_test_channel_connectivity',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/channel_connectivity.c'
      ]
    },
    {
      'target_name': 'end2end_test_compressed_payload',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/compressed_payload.c'
      ]
    },
    {
      'target_name': 'end2end_test_default_host',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/default_host.c'
      ]
    },
    {
      'target_name': 'end2end_test_disappearing_server',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/disappearing_server.c'
      ]
    },
    {
      'target_name': 'end2end_test_empty_batch',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/empty_batch.c'
      ]
    },
    {
      'target_name': 'end2end_test_graceful_server_shutdown',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/graceful_server_shutdown.c'
      ]
    },
    {
      'target_name': 'end2end_test_high_initial_seqno',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/high_initial_seqno.c'
      ]
    },
    {
      'target_name': 'end2end_test_invoke_large_request',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/invoke_large_request.c'
      ]
    },
    {
      'target_name': 'end2end_test_large_metadata',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/large_metadata.c'
      ]
    },
    {
      'target_name': 'end2end_test_max_concurrent_streams',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/max_concurrent_streams.c'
      ]
    },
    {
      'target_name': 'end2end_test_max_message_length',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/max_message_length.c'
      ]
    },
    {
      'target_name': 'end2end_test_metadata',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/metadata.c'
      ]
    },
    {
      'target_name': 'end2end_test_no_op',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/no_op.c'
      ]
    },
    {
      'target_name': 'end2end_test_payload',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/payload.c'
      ]
    },
    {
      'target_name': 'end2end_test_ping_pong_streaming',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/ping_pong_streaming.c'
      ]
    },
    {
      'target_name': 'end2end_test_registered_call',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/registered_call.c'
      ]
    },
    {
      'target_name': 'end2end_test_request_with_flags',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/request_with_flags.c'
      ]
    },
    {
      'target_name': 'end2end_test_request_with_payload',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/request_with_payload.c'
      ]
    },
    {
      'target_name': 'end2end_test_server_finishes_request',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/server_finishes_request.c'
      ]
    },
    {
      'target_name': 'end2end_test_shutdown_finishes_calls',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/shutdown_finishes_calls.c'
      ]
    },
    {
      'target_name': 'end2end_test_shutdown_finishes_tags',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/shutdown_finishes_tags.c'
      ]
    },
    {
      'target_name': 'end2end_test_simple_delayed_request',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/simple_delayed_request.c'
      ]
    },
    {
      'target_name': 'end2end_test_simple_request',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/simple_request.c'
      ]
    },
    {
      'target_name': 'end2end_test_trailing_metadata',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/tests/trailing_metadata.c'
      ]
    },
    {
      'target_name': 'end2end_certs',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
      ],
      'sources': [
        'test/core/end2end/data/test_root_cert.c'
        'test/core/end2end/data/server1_cert.c'
        'test/core/end2end/data/server1_key.c'
      ]
    },
    {
      'target_name': 'bad_client_test',
      'product_prefix': 'lib',
      'type': 'static_library',
      'dependencies': [
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/bad_client/bad_client.c'
      ]
    },
    {
      'target_name': 'alarm_heap_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/alarm_heap_test.c',
      ]
    },
    {
      'target_name': 'alarm_list_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/alarm_list_test.c',
      ]
    },
    {
      'target_name': 'alarm_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/alarm_test.c',
      ]
    },
    {
      'target_name': 'alpn_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/alpn_test.c',
      ]
    },
    {
      'target_name': 'bin_encoder_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/bin_encoder_test.c',
      ]
    },
    {
      'target_name': 'chttp2_status_conversion_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/status_conversion_test.c',
      ]
    },
    {
      'target_name': 'chttp2_stream_encoder_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/stream_encoder_test.c',
      ]
    },
    {
      'target_name': 'chttp2_stream_map_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/stream_map_test.c',
      ]
    },
    {
      'target_name': 'compression_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/compression/compression_test.c',
      ]
    },
    {
      'target_name': 'dualstack_socket_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/dualstack_socket_test.c',
      ]
    },
    {
      'target_name': 'endpoint_pair_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/endpoint_pair_test.c',
      ]
    },
    {
      'target_name': 'fd_conservation_posix_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/fd_conservation_posix_test.c',
      ]
    },
    {
      'target_name': 'fd_posix_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/fd_posix_test.c',
      ]
    },
    {
      'target_name': 'fling_client',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/fling/client.c',
      ]
    },
    {
      'target_name': 'fling_server',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/fling/server.c',
      ]
    },
    {
      'target_name': 'fling_stream_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/fling/fling_stream_test.c',
      ]
    },
    {
      'target_name': 'fling_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/fling/fling_test.c',
      ]
    },
    {
      'target_name': 'gen_hpack_tables',
      'type': 'executable',
      'dependencies': [
        'gpr',
        'grpc',
      ],
      'sources': [
        'tools/codegen/core/gen_hpack_tables.c',
      ]
    },
    {
      'target_name': 'gen_legal_metadata_characters',
      'type': 'executable',
      'dependencies': [
      ],
      'sources': [
        'tools/codegen/core/gen_legal_metadata_characters.c',
      ]
    },
    {
      'target_name': 'gpr_cmdline_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/cmdline_test.c',
      ]
    },
    {
      'target_name': 'gpr_env_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/env_test.c',
      ]
    },
    {
      'target_name': 'gpr_file_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/file_test.c',
      ]
    },
    {
      'target_name': 'gpr_histogram_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/histogram_test.c',
      ]
    },
    {
      'target_name': 'gpr_host_port_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/host_port_test.c',
      ]
    },
    {
      'target_name': 'gpr_log_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/log_test.c',
      ]
    },
    {
      'target_name': 'gpr_slice_buffer_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/slice_buffer_test.c',
      ]
    },
    {
      'target_name': 'gpr_slice_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/slice_test.c',
      ]
    },
    {
      'target_name': 'gpr_stack_lockfree_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/stack_lockfree_test.c',
      ]
    },
    {
      'target_name': 'gpr_string_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/string_test.c',
      ]
    },
    {
      'target_name': 'gpr_sync_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/sync_test.c',
      ]
    },
    {
      'target_name': 'gpr_thd_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/thd_test.c',
      ]
    },
    {
      'target_name': 'gpr_time_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/time_test.c',
      ]
    },
    {
      'target_name': 'gpr_tls_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/tls_test.c',
      ]
    },
    {
      'target_name': 'gpr_useful_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/useful_test.c',
      ]
    },
    {
      'target_name': 'grpc_auth_context_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/auth_context_test.c',
      ]
    },
    {
      'target_name': 'grpc_base64_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/base64_test.c',
      ]
    },
    {
      'target_name': 'grpc_byte_buffer_reader_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/surface/byte_buffer_reader_test.c',
      ]
    },
    {
      'target_name': 'grpc_channel_args_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/channel/channel_args_test.c',
      ]
    },
    {
      'target_name': 'grpc_channel_stack_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/channel/channel_stack_test.c',
      ]
    },
    {
      'target_name': 'grpc_completion_queue_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/surface/completion_queue_test.c',
      ]
    },
    {
      'target_name': 'grpc_create_jwt',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/create_jwt.c',
      ]
    },
    {
      'target_name': 'grpc_credentials_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/credentials_test.c',
      ]
    },
    {
      'target_name': 'grpc_fetch_oauth2',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/fetch_oauth2.c',
      ]
    },
    {
      'target_name': 'grpc_json_token_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/json_token_test.c',
      ]
    },
    {
      'target_name': 'grpc_jwt_verifier_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/jwt_verifier_test.c',
      ]
    },
    {
      'target_name': 'grpc_print_google_default_creds_token',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/print_google_default_creds_token.c',
      ]
    },
    {
      'target_name': 'grpc_security_connector_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/security_connector_test.c',
      ]
    },
    {
      'target_name': 'grpc_stream_op_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/stream_op_test.c',
      ]
    },
    {
      'target_name': 'grpc_verify_jwt',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/verify_jwt.c',
      ]
    },
    {
      'target_name': 'hpack_parser_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/hpack_parser_test.c',
      ]
    },
    {
      'target_name': 'hpack_table_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/hpack_table_test.c',
      ]
    },
    {
      'target_name': 'httpcli_format_request_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/httpcli/format_request_test.c',
      ]
    },
    {
      'target_name': 'httpcli_parser_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/httpcli/parser_test.c',
      ]
    },
    {
      'target_name': 'httpcli_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/httpcli/httpcli_test.c',
      ]
    },
    {
      'target_name': 'json_rewrite',
      'type': 'executable',
      'dependencies': [
        'grpc',
        'gpr',
      ],
      'sources': [
        'test/core/json/json_rewrite.c',
      ]
    },
    {
      'target_name': 'json_rewrite_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/json/json_rewrite_test.c',
      ]
    },
    {
      'target_name': 'json_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/json/json_test.c',
      ]
    },
    {
      'target_name': 'lame_client_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/surface/lame_client_test.c',
      ]
    },
    {
      'target_name': 'lb_policies_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/client_config/lb_policies_test.c',
      ]
    },
    {
      'target_name': 'low_level_ping_pong_benchmark',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/network_benchmarks/low_level_ping_pong.c',
      ]
    },
    {
      'target_name': 'message_compress_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/compression/message_compress_test.c',
      ]
    },
    {
      'target_name': 'multi_init_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/surface/multi_init_test.c',
      ]
    },
    {
      'target_name': 'multiple_server_queues_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/multiple_server_queues_test.c',
      ]
    },
    {
      'target_name': 'murmur_hash_test',
      'type': 'executable',
      'dependencies': [
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/support/murmur_hash_test.c',
      ]
    },
    {
      'target_name': 'no_server_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/end2end/no_server_test.c',
      ]
    },
    {
      'target_name': 'resolve_address_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/resolve_address_test.c',
      ]
    },
    {
      'target_name': 'secure_endpoint_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/security/secure_endpoint_test.c',
      ]
    },
    {
      'target_name': 'sockaddr_utils_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/sockaddr_utils_test.c',
      ]
    },
    {
      'target_name': 'tcp_client_posix_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/tcp_client_posix_test.c',
      ]
    },
    {
      'target_name': 'tcp_posix_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/tcp_posix_test.c',
      ]
    },
    {
      'target_name': 'tcp_server_posix_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/tcp_server_posix_test.c',
      ]
    },
    {
      'target_name': 'time_averaged_stats_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/time_averaged_stats_test.c',
      ]
    },
    {
      'target_name': 'timeout_encoding_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/chttp2/timeout_encoding_test.c',
      ]
    },
    {
      'target_name': 'timers_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/profiling/timers_test.c',
      ]
    },
    {
      'target_name': 'transport_metadata_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/transport/metadata_test.c',
      ]
    },
    {
      'target_name': 'transport_security_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/tsi/transport_security_test.c',
      ]
    },
    {
      'target_name': 'udp_server_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/iomgr/udp_server_test.c',
      ]
    },
    {
      'target_name': 'uri_parser_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/client_config/uri_parser_test.c',
      ]
    },
    {
      'target_name': 'async_end2end_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/async_end2end_test.cc',
      ]
    },
    {
      'target_name': 'async_streaming_ping_pong_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/qps/async_streaming_ping_pong_test.cc',
      ]
    },
    {
      'target_name': 'async_unary_ping_pong_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/qps/async_unary_ping_pong_test.cc',
      ]
    },
    {
      'target_name': 'auth_property_iterator_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/common/auth_property_iterator_test.cc',
      ]
    },
    {
      'target_name': 'channel_arguments_test',
      'type': 'executable',
      'dependencies': [
        'grpc++',
        'grpc',
        'gpr',
      ],
      'sources': [
        'test/cpp/client/channel_arguments_test.cc',
      ]
    },
    {
      'target_name': 'cli_call_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/util/cli_call_test.cc',
      ]
    },
    {
      'target_name': 'client_crash_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/client_crash_test.cc',
      ]
    },
    {
      'target_name': 'client_crash_test_server',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/client_crash_test_server.cc',
      ]
    },
    {
      'target_name': 'credentials_test',
      'type': 'executable',
      'dependencies': [
        'grpc++',
        'grpc',
        'gpr',
      ],
      'sources': [
        'test/cpp/client/credentials_test.cc',
      ]
    },
    {
      'target_name': 'cxx_byte_buffer_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/util/byte_buffer_test.cc',
      ]
    },
    {
      'target_name': 'cxx_slice_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/util/slice_test.cc',
      ]
    },
    {
      'target_name': 'cxx_string_ref_test',
      'type': 'executable',
      'dependencies': [
        'grpc++',
      ],
      'sources': [
        'test/cpp/util/string_ref_test.cc',
      ]
    },
    {
      'target_name': 'cxx_time_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/util/time_test.cc',
      ]
    },
    {
      'target_name': 'end2end_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/end2end_test.cc',
      ]
    },
    {
      'target_name': 'generic_end2end_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/generic_end2end_test.cc',
      ]
    },
    {
      'target_name': 'grpc_cli',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/cpp/util/grpc_cli.cc',
      ]
    },
    {
      'target_name': 'grpc_cpp_plugin',
      'type': 'executable',
      'dependencies': [
        'grpc_plugin_support',
      ],
      'sources': [
        'src/compiler/cpp_plugin.cc',
      ]
    },
    {
      'target_name': 'grpc_csharp_plugin',
      'type': 'executable',
      'dependencies': [
        'grpc_plugin_support',
      ],
      'sources': [
        'src/compiler/csharp_plugin.cc',
      ]
    },
    {
      'target_name': 'grpc_objective_c_plugin',
      'type': 'executable',
      'dependencies': [
        'grpc_plugin_support',
      ],
      'sources': [
        'src/compiler/objective_c_plugin.cc',
      ]
    },
    {
      'target_name': 'grpc_python_plugin',
      'type': 'executable',
      'dependencies': [
        'grpc_plugin_support',
      ],
      'sources': [
        'src/compiler/python_plugin.cc',
      ]
    },
    {
      'target_name': 'grpc_ruby_plugin',
      'type': 'executable',
      'dependencies': [
        'grpc_plugin_support',
      ],
      'sources': [
        'src/compiler/ruby_plugin.cc',
      ]
    },
    {
      'target_name': 'interop_client',
      'type': 'executable',
      'dependencies': [
        'interop_client_main',
        'interop_client_helper',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'interop_server',
      'type': 'executable',
      'dependencies': [
        'interop_server_main',
        'interop_server_helper',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'interop_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/interop/interop_test.cc',
      ]
    },
    {
      'target_name': 'mock_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/mock_test.cc',
      ]
    },
    {
      'target_name': 'qps_driver',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/cpp/qps/qps_driver.cc',
      ]
    },
    {
      'target_name': 'qps_interarrival_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/qps/qps_interarrival_test.cc',
      ]
    },
    {
      'target_name': 'qps_openloop_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/cpp/qps/qps_openloop_test.cc',
      ]
    },
    {
      'target_name': 'qps_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/cpp/qps/qps_test.cc',
      ]
    },
    {
      'target_name': 'qps_worker',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/cpp/qps/worker.cc',
      ]
    },
    {
      'target_name': 'reconnect_interop_client',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/proto/empty.proto',
        'test/proto/messages.proto',
        'test/proto/test.proto',
        'test/cpp/interop/reconnect_interop_client.cc',
      ]
    },
    {
      'target_name': 'reconnect_interop_server',
      'type': 'executable',
      'dependencies': [
        'reconnect_server',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
        'grpc++_test_config',
      ],
      'sources': [
        'test/proto/empty.proto',
        'test/proto/messages.proto',
        'test/proto/test.proto',
        'test/cpp/interop/reconnect_interop_server.cc',
      ]
    },
    {
      'target_name': 'secure_auth_context_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/common/secure_auth_context_test.cc',
      ]
    },
    {
      'target_name': 'server_crash_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/server_crash_test.cc',
      ]
    },
    {
      'target_name': 'server_crash_test_client',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/server_crash_test_client.cc',
      ]
    },
    {
      'target_name': 'shutdown_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/shutdown_test.cc',
      ]
    },
    {
      'target_name': 'status_test',
      'type': 'executable',
      'dependencies': [
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/util/status_test.cc',
      ]
    },
    {
      'target_name': 'streaming_throughput_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/streaming_throughput_test.cc',
      ]
    },
    {
      'target_name': 'sync_streaming_ping_pong_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/qps/sync_streaming_ping_pong_test.cc',
      ]
    },
    {
      'target_name': 'sync_unary_ping_pong_test',
      'type': 'executable',
      'dependencies': [
        'qps',
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/qps/sync_unary_ping_pong_test.cc',
      ]
    },
    {
      'target_name': 'thread_stress_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/thread_stress_test.cc',
      ]
    },
    {
      'target_name': 'zookeeper_test',
      'type': 'executable',
      'dependencies': [
        'grpc++_test_util',
        'grpc_test_util',
        'grpc++',
        'grpc_zookeeper',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/cpp/end2end/zookeeper_test.cc',
      ]
    },
    {
      'target_name': 'h2_compress_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_fakesec_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_fakesec',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_oauth2_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_oauth2',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl+poll_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl+poll',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_default_host_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_default_host',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_ssl_proxy_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_ssl_proxy',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_bad_hostname_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_bad_hostname',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_binary_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_binary_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_call_creds_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_call_creds',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_accept_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_accept',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_client_done_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_client_done',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_before_invoke_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_before_invoke',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_in_a_vacuum_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_in_a_vacuum',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_census_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_census_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_channel_connectivity_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_channel_connectivity',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_compressed_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_compressed_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_disappearing_server_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_disappearing_server',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_empty_batch_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_empty_batch',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_graceful_server_shutdown_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_graceful_server_shutdown',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_high_initial_seqno_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_high_initial_seqno',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_invoke_large_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_invoke_large_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_large_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_large_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_max_concurrent_streams_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_max_concurrent_streams',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_max_message_length_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_max_message_length',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_no_op_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_no_op',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_ping_pong_streaming_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_ping_pong_streaming',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_registered_call_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_registered_call',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_request_with_flags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_request_with_flags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_request_with_payload_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_request_with_payload',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_server_finishes_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_server_finishes_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_shutdown_finishes_calls_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_shutdown_finishes_calls',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_shutdown_finishes_tags_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_shutdown_finishes_tags',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_simple_delayed_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_simple_delayed_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_simple_request_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_simple_request',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_trailing_metadata_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_trailing_metadata',
        'end2end_certs',
        'grpc_test_util',
        'grpc',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_channel_connectivity_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_channel_connectivity',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_default_host_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_default_host',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_compress_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_compress',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_channel_connectivity_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_channel_connectivity',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_default_host_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_default_host',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_channel_connectivity_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_channel_connectivity',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_default_host_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_default_host',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_full+poll_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_full+poll',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_default_host_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_default_host',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_proxy_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_proxy',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair+trace_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair+trace',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_sockpair_1byte_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_sockpair_1byte',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_channel_connectivity_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_channel_connectivity',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_bad_hostname_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_bad_hostname',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_binary_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_binary_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_accept_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_accept',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_client_done_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_client_done',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_after_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_after_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_before_invoke_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_before_invoke',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_cancel_in_a_vacuum_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_cancel_in_a_vacuum',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_census_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_census_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_channel_connectivity_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_channel_connectivity',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_compressed_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_compressed_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_disappearing_server_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_disappearing_server',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_empty_batch_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_empty_batch',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_graceful_server_shutdown_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_graceful_server_shutdown',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_high_initial_seqno_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_high_initial_seqno',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_invoke_large_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_invoke_large_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_large_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_large_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_max_concurrent_streams_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_max_concurrent_streams',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_max_message_length_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_max_message_length',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_no_op_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_no_op',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_ping_pong_streaming_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_ping_pong_streaming',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_registered_call_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_registered_call',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_request_with_flags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_request_with_flags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_request_with_payload_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_request_with_payload',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_server_finishes_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_server_finishes_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_shutdown_finishes_calls_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_shutdown_finishes_calls',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_shutdown_finishes_tags_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_shutdown_finishes_tags',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_simple_delayed_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_simple_delayed_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_simple_request_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_simple_request',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'h2_uds+poll_trailing_metadata_nosec_test',
      'type': 'executable',
      'dependencies': [
        'end2end_fixture_h2_uds+poll',
        'end2end_test_trailing_metadata',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
      ]
    },
    {
      'target_name': 'connection_prefix_bad_client_test',
      'type': 'executable',
      'dependencies': [
        'bad_client_test',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/bad_client/tests/connection_prefix.c',
      ]
    },
    {
      'target_name': 'initial_settings_frame_bad_client_test',
      'type': 'executable',
      'dependencies': [
        'bad_client_test',
        'grpc_test_util_unsecure',
        'grpc_unsecure',
        'gpr_test_util',
        'gpr',
      ],
      'sources': [
        'test/core/bad_client/tests/initial_settings_frame.c',
      ]
    },
  ]
}
