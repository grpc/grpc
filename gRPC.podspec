# GRPC CocoaPods podspec
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


Pod::Spec.new do |s|
  s.name     = 'gRPC'
  s.version  = '0.7.0'
  s.summary  = 'gRPC client library for iOS/OSX'
  s.homepage = 'http://www.grpc.io'
  s.license  = 'New BSD'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  # s.source = { :git => 'https://github.com/grpc/grpc.git',
  #              :tag => 'release-0_10_0-objectivec-0.6.0' }

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'
  s.requires_arc = true

  objc_dir = 'src/objective-c'

  # Reactive Extensions library for iOS.
  s.subspec 'RxLibrary' do |ss|
    src_dir = "#{objc_dir}/RxLibrary"
    ss.source_files = "#{src_dir}/*.{h,m}", "#{src_dir}/**/*.{h,m}"
    ss.private_header_files = "#{src_dir}/private/*.h"
    ss.header_mappings_dir = "#{objc_dir}"
  end

  # Core cross-platform gRPC library, written in C.
  s.subspec 'C-Core' do |ss|
    ss.source_files = 'src/core/support/env.h',
                      'src/core/support/file.h',
                      'src/core/support/murmur_hash.h',
                      'src/core/support/stack_lockfree.h',
                      'src/core/support/grpc_string.h',
                      'src/core/support/string_win32.h',
                      'src/core/support/thd_internal.h',
                      'grpc/support/alloc.h',
                      'grpc/support/atm.h',
                      'grpc/support/atm_gcc_atomic.h',
                      'grpc/support/atm_gcc_sync.h',
                      'grpc/support/atm_win32.h',
                      'grpc/support/cmdline.h',
                      'grpc/support/cpu.h',
                      'grpc/support/histogram.h',
                      'grpc/support/host_port.h',
                      'grpc/support/log.h',
                      'grpc/support/log_win32.h',
                      'grpc/support/port_platform.h',
                      'grpc/support/slice.h',
                      'grpc/support/slice_buffer.h',
                      'grpc/support/string_util.h',
                      'grpc/support/subprocess.h',
                      'grpc/support/sync.h',
                      'grpc/support/sync_generic.h',
                      'grpc/support/sync_posix.h',
                      'grpc/support/sync_win32.h',
                      'grpc/support/thd.h',
                      'grpc/support/grpc_time.h',
                      'grpc/support/tls.h',
                      'grpc/support/tls_gcc.h',
                      'grpc/support/tls_msvc.h',
                      'grpc/support/tls_pthread.h',
                      'grpc/support/useful.h',
                      'src/core/support/alloc.c',
                      'src/core/support/cmdline.c',
                      'src/core/support/cpu_iphone.c',
                      'src/core/support/cpu_linux.c',
                      'src/core/support/cpu_posix.c',
                      'src/core/support/cpu_windows.c',
                      'src/core/support/env_linux.c',
                      'src/core/support/env_posix.c',
                      'src/core/support/env_win32.c',
                      'src/core/support/file.c',
                      'src/core/support/file_posix.c',
                      'src/core/support/file_win32.c',
                      'src/core/support/histogram.c',
                      'src/core/support/host_port.c',
                      'src/core/support/log.c',
                      'src/core/support/log_android.c',
                      'src/core/support/log_linux.c',
                      'src/core/support/log_posix.c',
                      'src/core/support/log_win32.c',
                      'src/core/support/murmur_hash.c',
                      'src/core/support/slice.c',
                      'src/core/support/slice_buffer.c',
                      'src/core/support/stack_lockfree.c',
                      'src/core/support/string.c',
                      'src/core/support/string_posix.c',
                      'src/core/support/string_win32.c',
                      'src/core/support/subprocess_posix.c',
                      'src/core/support/sync.c',
                      'src/core/support/sync_posix.c',
                      'src/core/support/sync_win32.c',
                      'src/core/support/thd.c',
                      'src/core/support/thd_posix.c',
                      'src/core/support/thd_win32.c',
                      'src/core/support/time.c',
                      'src/core/support/time_posix.c',
                      'src/core/support/time_win32.c',
                      'src/core/support/tls_pthread.c',
                      'src/core/security/auth_filters.h',
                      'src/core/security/base64.h',
                      'src/core/security/credentials.h',
                      'src/core/security/json_token.h',
                      'src/core/security/jwt_verifier.h',
                      'src/core/security/secure_endpoint.h',
                      'src/core/security/secure_transport_setup.h',
                      'src/core/security/security_connector.h',
                      'src/core/security/security_context.h',
                      'src/core/tsi/fake_transport_security.h',
                      'src/core/tsi/ssl_transport_security.h',
                      'src/core/tsi/transport_security.h',
                      'src/core/tsi/transport_security_interface.h',
                      'src/core/channel/census_filter.h',
                      'src/core/channel/channel_args.h',
                      'src/core/channel/channel_stack.h',
                      'src/core/channel/client_channel.h',
                      'src/core/channel/compress_filter.h',
                      'src/core/channel/connected_channel.h',
                      'src/core/channel/context.h',
                      'src/core/channel/http_client_filter.h',
                      'src/core/channel/http_server_filter.h',
                      'src/core/channel/noop_filter.h',
                      'src/core/client_config/client_config.h',
                      'src/core/client_config/connector.h',
                      'src/core/client_config/lb_policies/pick_first.h',
                      'src/core/client_config/lb_policy.h',
                      'src/core/client_config/resolver.h',
                      'src/core/client_config/resolver_factory.h',
                      'src/core/client_config/resolver_registry.h',
                      'src/core/client_config/resolvers/dns_resolver.h',
                      'src/core/client_config/resolvers/sockaddr_resolver.h',
                      'src/core/client_config/subchannel.h',
                      'src/core/client_config/subchannel_factory.h',
                      'src/core/client_config/subchannel_factory_decorators/add_channel_arg.h',
                      'src/core/client_config/subchannel_factory_decorators/merge_channel_args.h',
                      'src/core/client_config/uri_parser.h',
                      'src/core/compression/message_compress.h',
                      'src/core/debug/trace.h',
                      'src/core/httpcli/format_request.h',
                      'src/core/httpcli/httpcli.h',
                      'src/core/httpcli/parser.h',
                      'src/core/iomgr/alarm.h',
                      'src/core/iomgr/alarm_heap.h',
                      'src/core/iomgr/alarm_internal.h',
                      'src/core/iomgr/endpoint.h',
                      'src/core/iomgr/endpoint_pair.h',
                      'src/core/iomgr/fd_posix.h',
                      'src/core/iomgr/iocp_windows.h',
                      'src/core/iomgr/iomgr.h',
                      'src/core/iomgr/iomgr_internal.h',
                      'src/core/iomgr/iomgr_posix.h',
                      'src/core/iomgr/pollset.h',
                      'src/core/iomgr/pollset_posix.h',
                      'src/core/iomgr/pollset_set.h',
                      'src/core/iomgr/pollset_set_posix.h',
                      'src/core/iomgr/pollset_set_windows.h',
                      'src/core/iomgr/pollset_windows.h',
                      'src/core/iomgr/resolve_address.h',
                      'src/core/iomgr/sockaddr.h',
                      'src/core/iomgr/sockaddr_posix.h',
                      'src/core/iomgr/sockaddr_utils.h',
                      'src/core/iomgr/sockaddr_win32.h',
                      'src/core/iomgr/socket_utils_posix.h',
                      'src/core/iomgr/socket_windows.h',
                      'src/core/iomgr/tcp_client.h',
                      'src/core/iomgr/tcp_posix.h',
                      'src/core/iomgr/tcp_server.h',
                      'src/core/iomgr/tcp_windows.h',
                      'src/core/iomgr/time_averaged_stats.h',
                      'src/core/iomgr/udp_server.h',
                      'src/core/iomgr/wakeup_fd_pipe.h',
                      'src/core/iomgr/wakeup_fd_posix.h',
                      'src/core/json/json.h',
                      'src/core/json/json_common.h',
                      'src/core/json/json_reader.h',
                      'src/core/json/json_writer.h',
                      'src/core/profiling/timers.h',
                      'src/core/profiling/timers_preciseclock.h',
                      'src/core/surface/byte_buffer_queue.h',
                      'src/core/surface/call.h',
                      'src/core/surface/channel.h',
                      'src/core/surface/completion_queue.h',
                      'src/core/surface/event_string.h',
                      'src/core/surface/init.h',
                      'src/core/surface/server.h',
                      'src/core/surface/surface_trace.h',
                      'src/core/transport/chttp2/alpn.h',
                      'src/core/transport/chttp2/bin_encoder.h',
                      'src/core/transport/chttp2/frame.h',
                      'src/core/transport/chttp2/frame_data.h',
                      'src/core/transport/chttp2/frame_goaway.h',
                      'src/core/transport/chttp2/frame_ping.h',
                      'src/core/transport/chttp2/frame_rst_stream.h',
                      'src/core/transport/chttp2/frame_settings.h',
                      'src/core/transport/chttp2/frame_window_update.h',
                      'src/core/transport/chttp2/hpack_parser.h',
                      'src/core/transport/chttp2/hpack_table.h',
                      'src/core/transport/chttp2/http2_errors.h',
                      'src/core/transport/chttp2/huffsyms.h',
                      'src/core/transport/chttp2/incoming_metadata.h',
                      'src/core/transport/chttp2/internal.h',
                      'src/core/transport/chttp2/status_conversion.h',
                      'src/core/transport/chttp2/stream_encoder.h',
                      'src/core/transport/chttp2/stream_map.h',
                      'src/core/transport/chttp2/timeout_encoding.h',
                      'src/core/transport/chttp2/varint.h',
                      'src/core/transport/chttp2_transport.h',
                      'src/core/transport/connectivity_state.h',
                      'src/core/transport/metadata.h',
                      'src/core/transport/stream_op.h',
                      'src/core/transport/transport.h',
                      'src/core/transport/transport_impl.h',
                      'src/core/census/context.h',
                      'src/core/census/rpc_stat_id.h',
                      'grpc/grpc_security.h',
                      'grpc/byte_buffer.h',
                      'grpc/byte_buffer_reader.h',
                      'grpc/compression.h',
                      'grpc/grpc.h',
                      'grpc/status.h',
                      'grpc/census.h',
                      'src/core/httpcli/httpcli_security_connector.c',
                      'src/core/security/base64.c',
                      'src/core/security/client_auth_filter.c',
                      'src/core/security/credentials.c',
                      'src/core/security/credentials_metadata.c',
                      'src/core/security/credentials_posix.c',
                      'src/core/security/credentials_win32.c',
                      'src/core/security/google_default_credentials.c',
                      'src/core/security/json_token.c',
                      'src/core/security/jwt_verifier.c',
                      'src/core/security/secure_endpoint.c',
                      'src/core/security/secure_transport_setup.c',
                      'src/core/security/security_connector.c',
                      'src/core/security/security_context.c',
                      'src/core/security/server_auth_filter.c',
                      'src/core/security/server_secure_chttp2.c',
                      'src/core/surface/init_secure.c',
                      'src/core/surface/secure_channel_create.c',
                      'src/core/tsi/fake_transport_security.c',
                      'src/core/tsi/ssl_transport_security.c',
                      'src/core/tsi/transport_security.c',
                      'src/core/census/grpc_context.c',
                      'src/core/channel/channel_args.c',
                      'src/core/channel/channel_stack.c',
                      'src/core/channel/client_channel.c',
                      'src/core/channel/compress_filter.c',
                      'src/core/channel/connected_channel.c',
                      'src/core/channel/http_client_filter.c',
                      'src/core/channel/http_server_filter.c',
                      'src/core/channel/noop_filter.c',
                      'src/core/client_config/client_config.c',
                      'src/core/client_config/connector.c',
                      'src/core/client_config/lb_policies/pick_first.c',
                      'src/core/client_config/lb_policy.c',
                      'src/core/client_config/resolver.c',
                      'src/core/client_config/resolver_factory.c',
                      'src/core/client_config/resolver_registry.c',
                      'src/core/client_config/resolvers/dns_resolver.c',
                      'src/core/client_config/resolvers/sockaddr_resolver.c',
                      'src/core/client_config/subchannel.c',
                      'src/core/client_config/subchannel_factory.c',
                      'src/core/client_config/subchannel_factory_decorators/add_channel_arg.c',
                      'src/core/client_config/subchannel_factory_decorators/merge_channel_args.c',
                      'src/core/client_config/uri_parser.c',
                      'src/core/compression/algorithm.c',
                      'src/core/compression/message_compress.c',
                      'src/core/debug/trace.c',
                      'src/core/httpcli/format_request.c',
                      'src/core/httpcli/httpcli.c',
                      'src/core/httpcli/parser.c',
                      'src/core/iomgr/alarm.c',
                      'src/core/iomgr/alarm_heap.c',
                      'src/core/iomgr/endpoint.c',
                      'src/core/iomgr/endpoint_pair_posix.c',
                      'src/core/iomgr/endpoint_pair_windows.c',
                      'src/core/iomgr/fd_posix.c',
                      'src/core/iomgr/iocp_windows.c',
                      'src/core/iomgr/iomgr.c',
                      'src/core/iomgr/iomgr_posix.c',
                      'src/core/iomgr/iomgr_windows.c',
                      'src/core/iomgr/pollset_multipoller_with_epoll.c',
                      'src/core/iomgr/pollset_multipoller_with_poll_posix.c',
                      'src/core/iomgr/pollset_posix.c',
                      'src/core/iomgr/pollset_set_posix.c',
                      'src/core/iomgr/pollset_set_windows.c',
                      'src/core/iomgr/pollset_windows.c',
                      'src/core/iomgr/resolve_address_posix.c',
                      'src/core/iomgr/resolve_address_windows.c',
                      'src/core/iomgr/sockaddr_utils.c',
                      'src/core/iomgr/socket_utils_common_posix.c',
                      'src/core/iomgr/socket_utils_linux.c',
                      'src/core/iomgr/socket_utils_posix.c',
                      'src/core/iomgr/socket_windows.c',
                      'src/core/iomgr/tcp_client_posix.c',
                      'src/core/iomgr/tcp_client_windows.c',
                      'src/core/iomgr/tcp_posix.c',
                      'src/core/iomgr/tcp_server_posix.c',
                      'src/core/iomgr/tcp_server_windows.c',
                      'src/core/iomgr/tcp_windows.c',
                      'src/core/iomgr/time_averaged_stats.c',
                      'src/core/iomgr/udp_server.c',
                      'src/core/iomgr/wakeup_fd_eventfd.c',
                      'src/core/iomgr/wakeup_fd_nospecial.c',
                      'src/core/iomgr/wakeup_fd_pipe.c',
                      'src/core/iomgr/wakeup_fd_posix.c',
                      'src/core/json/json.c',
                      'src/core/json/json_reader.c',
                      'src/core/json/json_string.c',
                      'src/core/json/json_writer.c',
                      'src/core/profiling/basic_timers.c',
                      'src/core/profiling/stap_timers.c',
                      'src/core/surface/byte_buffer.c',
                      'src/core/surface/byte_buffer_queue.c',
                      'src/core/surface/byte_buffer_reader.c',
                      'src/core/surface/call.c',
                      'src/core/surface/call_details.c',
                      'src/core/surface/call_log_batch.c',
                      'src/core/surface/channel.c',
                      'src/core/surface/channel_connectivity.c',
                      'src/core/surface/channel_create.c',
                      'src/core/surface/completion_queue.c',
                      'src/core/surface/event_string.c',
                      'src/core/surface/init.c',
                      'src/core/surface/lame_client.c',
                      'src/core/surface/metadata_array.c',
                      'src/core/surface/server.c',
                      'src/core/surface/server_chttp2.c',
                      'src/core/surface/server_create.c',
                      'src/core/surface/surface_trace.c',
                      'src/core/surface/version.c',
                      'src/core/transport/chttp2/alpn.c',
                      'src/core/transport/chttp2/bin_encoder.c',
                      'src/core/transport/chttp2/frame_data.c',
                      'src/core/transport/chttp2/frame_goaway.c',
                      'src/core/transport/chttp2/frame_ping.c',
                      'src/core/transport/chttp2/frame_rst_stream.c',
                      'src/core/transport/chttp2/frame_settings.c',
                      'src/core/transport/chttp2/frame_window_update.c',
                      'src/core/transport/chttp2/hpack_parser.c',
                      'src/core/transport/chttp2/hpack_table.c',
                      'src/core/transport/chttp2/huffsyms.c',
                      'src/core/transport/chttp2/incoming_metadata.c',
                      'src/core/transport/chttp2/parsing.c',
                      'src/core/transport/chttp2/status_conversion.c',
                      'src/core/transport/chttp2/stream_encoder.c',
                      'src/core/transport/chttp2/stream_lists.c',
                      'src/core/transport/chttp2/stream_map.c',
                      'src/core/transport/chttp2/timeout_encoding.c',
                      'src/core/transport/chttp2/varint.c',
                      'src/core/transport/chttp2/writing.c',
                      'src/core/transport/chttp2_transport.c',
                      'src/core/transport/connectivity_state.c',
                      'src/core/transport/metadata.c',
                      'src/core/transport/stream_op.c',
                      'src/core/transport/transport.c',
                      'src/core/transport/transport_op_string.c',
                      'src/core/census/context.c',
                      'src/core/census/initialize.c',
                      'src/core/census/record_stat.c'

    ss.private_header_files = 'src/core/support/env.h',
                              'src/core/support/file.h',
                              'src/core/support/murmur_hash.h',
                              'src/core/support/stack_lockfree.h',
                              'src/core/support/string.h',
                              'src/core/support/string_win32.h',
                              'src/core/support/thd_internal.h',
                              'src/core/security/auth_filters.h',
                              'src/core/security/base64.h',
                              'src/core/security/credentials.h',
                              'src/core/security/json_token.h',
                              'src/core/security/jwt_verifier.h',
                              'src/core/security/secure_endpoint.h',
                              'src/core/security/secure_transport_setup.h',
                              'src/core/security/security_connector.h',
                              'src/core/security/security_context.h',
                              'src/core/tsi/fake_transport_security.h',
                              'src/core/tsi/ssl_transport_security.h',
                              'src/core/tsi/transport_security.h',
                              'src/core/tsi/transport_security_interface.h',
                              'src/core/channel/census_filter.h',
                              'src/core/channel/channel_args.h',
                              'src/core/channel/channel_stack.h',
                              'src/core/channel/client_channel.h',
                              'src/core/channel/compress_filter.h',
                              'src/core/channel/connected_channel.h',
                              'src/core/channel/context.h',
                              'src/core/channel/http_client_filter.h',
                              'src/core/channel/http_server_filter.h',
                              'src/core/channel/noop_filter.h',
                              'src/core/client_config/client_config.h',
                              'src/core/client_config/connector.h',
                              'src/core/client_config/lb_policies/pick_first.h',
                              'src/core/client_config/lb_policy.h',
                              'src/core/client_config/resolver.h',
                              'src/core/client_config/resolver_factory.h',
                              'src/core/client_config/resolver_registry.h',
                              'src/core/client_config/resolvers/dns_resolver.h',
                              'src/core/client_config/resolvers/sockaddr_resolver.h',
                              'src/core/client_config/subchannel.h',
                              'src/core/client_config/subchannel_factory.h',
                              'src/core/client_config/subchannel_factory_decorators/add_channel_arg.h',
                              'src/core/client_config/subchannel_factory_decorators/merge_channel_args.h',
                              'src/core/client_config/uri_parser.h',
                              'src/core/compression/message_compress.h',
                              'src/core/debug/trace.h',
                              'src/core/httpcli/format_request.h',
                              'src/core/httpcli/httpcli.h',
                              'src/core/httpcli/parser.h',
                              'src/core/iomgr/alarm.h',
                              'src/core/iomgr/alarm_heap.h',
                              'src/core/iomgr/alarm_internal.h',
                              'src/core/iomgr/endpoint.h',
                              'src/core/iomgr/endpoint_pair.h',
                              'src/core/iomgr/fd_posix.h',
                              'src/core/iomgr/iocp_windows.h',
                              'src/core/iomgr/iomgr.h',
                              'src/core/iomgr/iomgr_internal.h',
                              'src/core/iomgr/iomgr_posix.h',
                              'src/core/iomgr/pollset.h',
                              'src/core/iomgr/pollset_posix.h',
                              'src/core/iomgr/pollset_set.h',
                              'src/core/iomgr/pollset_set_posix.h',
                              'src/core/iomgr/pollset_set_windows.h',
                              'src/core/iomgr/pollset_windows.h',
                              'src/core/iomgr/resolve_address.h',
                              'src/core/iomgr/sockaddr.h',
                              'src/core/iomgr/sockaddr_posix.h',
                              'src/core/iomgr/sockaddr_utils.h',
                              'src/core/iomgr/sockaddr_win32.h',
                              'src/core/iomgr/socket_utils_posix.h',
                              'src/core/iomgr/socket_windows.h',
                              'src/core/iomgr/tcp_client.h',
                              'src/core/iomgr/tcp_posix.h',
                              'src/core/iomgr/tcp_server.h',
                              'src/core/iomgr/tcp_windows.h',
                              'src/core/iomgr/time_averaged_stats.h',
                              'src/core/iomgr/udp_server.h',
                              'src/core/iomgr/wakeup_fd_pipe.h',
                              'src/core/iomgr/wakeup_fd_posix.h',
                              'src/core/json/json.h',
                              'src/core/json/json_common.h',
                              'src/core/json/json_reader.h',
                              'src/core/json/json_writer.h',
                              'src/core/profiling/timers.h',
                              'src/core/profiling/timers_preciseclock.h',
                              'src/core/surface/byte_buffer_queue.h',
                              'src/core/surface/call.h',
                              'src/core/surface/channel.h',
                              'src/core/surface/completion_queue.h',
                              'src/core/surface/event_string.h',
                              'src/core/surface/init.h',
                              'src/core/surface/server.h',
                              'src/core/surface/surface_trace.h',
                              'src/core/transport/chttp2/alpn.h',
                              'src/core/transport/chttp2/bin_encoder.h',
                              'src/core/transport/chttp2/frame.h',
                              'src/core/transport/chttp2/frame_data.h',
                              'src/core/transport/chttp2/frame_goaway.h',
                              'src/core/transport/chttp2/frame_ping.h',
                              'src/core/transport/chttp2/frame_rst_stream.h',
                              'src/core/transport/chttp2/frame_settings.h',
                              'src/core/transport/chttp2/frame_window_update.h',
                              'src/core/transport/chttp2/hpack_parser.h',
                              'src/core/transport/chttp2/hpack_table.h',
                              'src/core/transport/chttp2/http2_errors.h',
                              'src/core/transport/chttp2/huffsyms.h',
                              'src/core/transport/chttp2/incoming_metadata.h',
                              'src/core/transport/chttp2/internal.h',
                              'src/core/transport/chttp2/status_conversion.h',
                              'src/core/transport/chttp2/stream_encoder.h',
                              'src/core/transport/chttp2/stream_map.h',
                              'src/core/transport/chttp2/timeout_encoding.h',
                              'src/core/transport/chttp2/varint.h',
                              'src/core/transport/chttp2_transport.h',
                              'src/core/transport/connectivity_state.h',
                              'src/core/transport/metadata.h',
                              'src/core/transport/stream_op.h',
                              'src/core/transport/transport.h',
                              'src/core/transport/transport_impl.h',
                              'src/core/census/context.h',
                              'src/core/census/rpc_stat_id.h'

    ss.header_mappings_dir = '.'

    ss.requires_arc = false
    ss.libraries = 'z'
    ss.dependency 'OpenSSL', '~> 1.0.200'

    # ss.compiler_flags = '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w'
  end

  # This is a workaround for Cocoapods Issue #1437.
  # It renames time.h and string.h to grpc_time.h and grpc_string.h.
  # It needs to be here (top-level) instead of in the C-Core subspec because Cocoapods doesn't run
  # prepare_command's of subspecs.
  #
  # TODO(jcanizales): Try out others' solutions at Issue #1437.
  s.prepare_command = <<-CMD
    # Move contents of include up a level to avoid manually specifying include paths
    cp -r "include/grpc" "."

    DIR_TIME="grpc/support"
    BAD_TIME="$DIR_TIME/time.h"
    GOOD_TIME="$DIR_TIME/grpc_time.h"
    grep -rl "$BAD_TIME" grpc src/core src/objective-c/GRPCClient | xargs sed -i '' -e s@$BAD_TIME@$GOOD_TIME@g
    if [ -f "$BAD_TIME" ];
    then
      mv -f "$BAD_TIME" "$GOOD_TIME"
    fi

    DIR_STRING="src/core/support"
    BAD_STRING="$DIR_STRING/string.h"
    GOOD_STRING="$DIR_STRING/grpc_string.h"
    grep -rl "$BAD_STRING" grpc src/core src/objective-c/GRPCClient | xargs sed -i '' -e s@$BAD_STRING@$GOOD_STRING@g
    if [ -f "$BAD_STRING" ];
    then
      mv -f "$BAD_STRING" "$GOOD_STRING"
    fi
  CMD

  # Objective-C wrapper around the core gRPC library.
  s.subspec 'GRPCClient' do |ss|
    src_dir = "#{objc_dir}/GRPCClient"
    ss.source_files = "#{src_dir}/*.{h,m}", "#{src_dir}/**/*.{h,m}"
    ss.private_header_files = "#{src_dir}/private/*.h"
    ss.header_mappings_dir = "#{objc_dir}"

    ss.dependency 'gRPC/C-Core'
    ss.dependency 'gRPC/RxLibrary'

    # Certificates, to be able to establish TLS connections:
    ss.resource_bundles = { 'gRPCCertificates' => ['etc/roots.pem'] }
  end

  # RPC library for ProtocolBuffers, based on gRPC
  s.subspec 'ProtoRPC' do |ss|
    src_dir = "#{objc_dir}/ProtoRPC"
    ss.source_files = "#{src_dir}/*.{h,m}"
    ss.header_mappings_dir = "#{objc_dir}"

    ss.dependency 'gRPC/GRPCClient'
    ss.dependency 'gRPC/RxLibrary'
    ss.dependency 'Protobuf', '~> 3.0.0-alpha-3'
  end
end
