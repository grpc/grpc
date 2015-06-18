

Pod::Spec.new do |s|
  s.name     = 'gRPC'
  s.version  = '0.6.0'
  s.summary  = 'gRPC client library for iOS/OSX'
  s.homepage = 'http://www.grpc.io'
  s.license  = 'New BSD'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  # s.source = { :git => 'https://github.com/grpc/grpc.git',
  #              :tag => 'release-0_9_1-objectivec-0.5.1' }

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'
  s.requires_arc = true

  # Reactive Extensions library for iOS.
  s.subspec 'RxLibrary' do |rs|
    rs.source_files = 'src/objective-c/RxLibrary/*.{h,m}',
                      'src/objective-c/RxLibrary/transformations/*.{h,m}',
                      'src/objective-c/RxLibrary/private/*.{h,m}'
    rs.private_header_files = 'src/objective-c/RxLibrary/private/*.h'
  end

  # Core cross-platform gRPC library, written in C.
  s.subspec 'C-Core' do |cs|
    cs.source_files = 'src/core/support/env.h', 'src/core/support/file.h', 'src/core/support/murmur_hash.h', 'src/core/support/grpc_string.h', 'src/core/support/string_win32.h', 'src/core/support/thd_internal.h', 'include/grpc/support/alloc.h', 'include/grpc/support/atm.h', 'include/grpc/support/atm_gcc_atomic.h', 'include/grpc/support/atm_gcc_sync.h', 'include/grpc/support/atm_win32.h', 'include/grpc/support/cancellable_platform.h', 'include/grpc/support/cmdline.h', 'include/grpc/support/cpu.h', 'include/grpc/support/histogram.h', 'include/grpc/support/host_port.h', 'include/grpc/support/log.h', 'include/grpc/support/log_win32.h', 'include/grpc/support/port_platform.h', 'include/grpc/support/slice.h', 'include/grpc/support/slice_buffer.h', 'include/grpc/support/string_util.h', 'include/grpc/support/subprocess.h', 'include/grpc/support/sync.h', 'include/grpc/support/sync_generic.h', 'include/grpc/support/sync_posix.h', 'include/grpc/support/sync_win32.h', 'include/grpc/support/thd.h', 'include/grpc/support/grpc_time.h', 'include/grpc/support/tls.h', 'include/grpc/support/tls_gcc.h', 'include/grpc/support/tls_msvc.h', 'include/grpc/support/tls_pthread.h', 'include/grpc/support/useful.h', 'src/core/support/alloc.c', 'src/core/support/cancellable.c', 'src/core/support/cmdline.c', 'src/core/support/cpu_iphone.c', 'src/core/support/cpu_linux.c', 'src/core/support/cpu_posix.c', 'src/core/support/cpu_windows.c', 'src/core/support/env_linux.c', 'src/core/support/env_posix.c', 'src/core/support/env_win32.c', 'src/core/support/file.c', 'src/core/support/file_posix.c', 'src/core/support/file_win32.c', 'src/core/support/histogram.c', 'src/core/support/host_port.c', 'src/core/support/log.c', 'src/core/support/log_android.c', 'src/core/support/log_linux.c', 'src/core/support/log_posix.c', 'src/core/support/log_win32.c', 'src/core/support/murmur_hash.c', 'src/core/support/slice.c', 'src/core/support/slice_buffer.c', 'src/core/support/string.c', 'src/core/support/string_posix.c', 'src/core/support/string_win32.c', 'src/core/support/subprocess_posix.c', 'src/core/support/sync.c', 'src/core/support/sync_posix.c', 'src/core/support/sync_win32.c', 'src/core/support/thd.c', 'src/core/support/thd_posix.c', 'src/core/support/thd_win32.c', 'src/core/support/time.c', 'src/core/support/time_posix.c', 'src/core/support/time_win32.c', 'src/core/support/tls_pthread.c', 'src/core/httpcli/format_request.h', 'src/core/httpcli/httpcli.h', 'src/core/httpcli/httpcli_security_connector.h', 'src/core/httpcli/parser.h', 'src/core/security/auth_filters.h', 'src/core/security/base64.h', 'src/core/security/credentials.h', 'src/core/security/json_token.h', 'src/core/security/secure_endpoint.h', 'src/core/security/secure_transport_setup.h', 'src/core/security/security_connector.h', 'src/core/security/security_context.h', 'src/core/tsi/fake_transport_security.h', 'src/core/tsi/ssl_transport_security.h', 'src/core/tsi/transport_security.h', 'src/core/tsi/transport_security_interface.h', 'src/core/census/grpc_context.h', 'src/core/channel/census_filter.h', 'src/core/channel/channel_args.h', 'src/core/channel/channel_stack.h', 'src/core/channel/child_channel.h', 'src/core/channel/client_channel.h', 'src/core/channel/client_setup.h', 'src/core/channel/connected_channel.h', 'src/core/channel/context.h', 'src/core/channel/http_client_filter.h', 'src/core/channel/http_server_filter.h', 'src/core/channel/noop_filter.h', 'src/core/compression/message_compress.h', 'src/core/debug/trace.h', 'src/core/iomgr/alarm.h', 'src/core/iomgr/alarm_heap.h', 'src/core/iomgr/alarm_internal.h', 'src/core/iomgr/endpoint.h', 'src/core/iomgr/endpoint_pair.h', 'src/core/iomgr/fd_posix.h', 'src/core/iomgr/iocp_windows.h', 'src/core/iomgr/iomgr.h', 'src/core/iomgr/iomgr_internal.h', 'src/core/iomgr/iomgr_posix.h', 'src/core/iomgr/pollset.h', 'src/core/iomgr/pollset_kick_posix.h', 'src/core/iomgr/pollset_posix.h', 'src/core/iomgr/pollset_set_posix.h', 'src/core/iomgr/pollset_set_windows.h', 'src/core/iomgr/pollset_windows.h', 'src/core/iomgr/resolve_address.h', 'src/core/iomgr/sockaddr.h', 'src/core/iomgr/sockaddr_posix.h', 'src/core/iomgr/sockaddr_utils.h', 'src/core/iomgr/sockaddr_win32.h', 'src/core/iomgr/socket_utils_posix.h', 'src/core/iomgr/socket_windows.h', 'src/core/iomgr/tcp_client.h', 'src/core/iomgr/tcp_posix.h', 'src/core/iomgr/tcp_server.h', 'src/core/iomgr/tcp_windows.h', 'src/core/iomgr/time_averaged_stats.h', 'src/core/iomgr/wakeup_fd_pipe.h', 'src/core/iomgr/wakeup_fd_posix.h', 'src/core/json/json.h', 'src/core/json/json_common.h', 'src/core/json/json_reader.h', 'src/core/json/json_writer.h', 'src/core/profiling/timers.h', 'src/core/profiling/timers_preciseclock.h', 'src/core/surface/byte_buffer_queue.h', 'src/core/surface/call.h', 'src/core/surface/channel.h', 'src/core/surface/client.h', 'src/core/surface/completion_queue.h', 'src/core/surface/event_string.h', 'src/core/surface/init.h', 'src/core/surface/server.h', 'src/core/surface/surface_trace.h', 'src/core/transport/chttp2/alpn.h', 'src/core/transport/chttp2/bin_encoder.h', 'src/core/transport/chttp2/frame.h', 'src/core/transport/chttp2/frame_data.h', 'src/core/transport/chttp2/frame_goaway.h', 'src/core/transport/chttp2/frame_ping.h', 'src/core/transport/chttp2/frame_rst_stream.h', 'src/core/transport/chttp2/frame_settings.h', 'src/core/transport/chttp2/frame_window_update.h', 'src/core/transport/chttp2/hpack_parser.h', 'src/core/transport/chttp2/hpack_table.h', 'src/core/transport/chttp2/http2_errors.h', 'src/core/transport/chttp2/huffsyms.h', 'src/core/transport/chttp2/incoming_metadata.h', 'src/core/transport/chttp2/internal.h', 'src/core/transport/chttp2/status_conversion.h', 'src/core/transport/chttp2/stream_encoder.h', 'src/core/transport/chttp2/stream_map.h', 'src/core/transport/chttp2/timeout_encoding.h', 'src/core/transport/chttp2/varint.h', 'src/core/transport/chttp2_transport.h', 'src/core/transport/metadata.h', 'src/core/transport/stream_op.h', 'src/core/transport/transport.h', 'src/core/transport/transport_impl.h', 'src/core/census/context.h', 'include/grpc/grpc_security.h', 'include/grpc/byte_buffer.h', 'include/grpc/byte_buffer_reader.h', 'include/grpc/compression.h', 'include/grpc/grpc.h', 'include/grpc/status.h', 'include/grpc/census.h', 'src/core/httpcli/format_request.c', 'src/core/httpcli/httpcli.c', 'src/core/httpcli/httpcli_security_connector.c', 'src/core/httpcli/parser.c', 'src/core/security/base64.c', 'src/core/security/client_auth_filter.c', 'src/core/security/credentials.c', 'src/core/security/credentials_metadata.c', 'src/core/security/credentials_posix.c', 'src/core/security/credentials_win32.c', 'src/core/security/google_default_credentials.c', 'src/core/security/json_token.c', 'src/core/security/secure_endpoint.c', 'src/core/security/secure_transport_setup.c', 'src/core/security/security_connector.c', 'src/core/security/security_context.c', 'src/core/security/server_auth_filter.c', 'src/core/security/server_secure_chttp2.c', 'src/core/surface/init_secure.c', 'src/core/surface/secure_channel_create.c', 'src/core/tsi/fake_transport_security.c', 'src/core/tsi/ssl_transport_security.c', 'src/core/tsi/transport_security.c', 'src/core/census/grpc_context.c', 'src/core/channel/channel_args.c', 'src/core/channel/channel_stack.c', 'src/core/channel/child_channel.c', 'src/core/channel/client_channel.c', 'src/core/channel/client_setup.c', 'src/core/channel/connected_channel.c', 'src/core/channel/http_client_filter.c', 'src/core/channel/http_server_filter.c', 'src/core/channel/noop_filter.c', 'src/core/compression/algorithm.c', 'src/core/compression/message_compress.c', 'src/core/debug/trace.c', 'src/core/iomgr/alarm.c', 'src/core/iomgr/alarm_heap.c', 'src/core/iomgr/endpoint.c', 'src/core/iomgr/endpoint_pair_posix.c', 'src/core/iomgr/endpoint_pair_windows.c', 'src/core/iomgr/fd_posix.c', 'src/core/iomgr/iocp_windows.c', 'src/core/iomgr/iomgr.c', 'src/core/iomgr/iomgr_posix.c', 'src/core/iomgr/iomgr_windows.c', 'src/core/iomgr/pollset_kick_posix.c', 'src/core/iomgr/pollset_multipoller_with_epoll.c', 'src/core/iomgr/pollset_multipoller_with_poll_posix.c', 'src/core/iomgr/pollset_posix.c', 'src/core/iomgr/pollset_set_posix.c', 'src/core/iomgr/pollset_set_windows.c', 'src/core/iomgr/pollset_windows.c', 'src/core/iomgr/resolve_address_posix.c', 'src/core/iomgr/resolve_address_windows.c', 'src/core/iomgr/sockaddr_utils.c', 'src/core/iomgr/socket_utils_common_posix.c', 'src/core/iomgr/socket_utils_linux.c', 'src/core/iomgr/socket_utils_posix.c', 'src/core/iomgr/socket_windows.c', 'src/core/iomgr/tcp_client_posix.c', 'src/core/iomgr/tcp_client_windows.c', 'src/core/iomgr/tcp_posix.c', 'src/core/iomgr/tcp_server_posix.c', 'src/core/iomgr/tcp_server_windows.c', 'src/core/iomgr/tcp_windows.c', 'src/core/iomgr/time_averaged_stats.c', 'src/core/iomgr/wakeup_fd_eventfd.c', 'src/core/iomgr/wakeup_fd_nospecial.c', 'src/core/iomgr/wakeup_fd_pipe.c', 'src/core/iomgr/wakeup_fd_posix.c', 'src/core/json/json.c', 'src/core/json/json_reader.c', 'src/core/json/json_string.c', 'src/core/json/json_writer.c', 'src/core/profiling/basic_timers.c', 'src/core/profiling/stap_timers.c', 'src/core/surface/byte_buffer.c', 'src/core/surface/byte_buffer_queue.c', 'src/core/surface/byte_buffer_reader.c', 'src/core/surface/call.c', 'src/core/surface/call_details.c', 'src/core/surface/call_log_batch.c', 'src/core/surface/channel.c', 'src/core/surface/channel_create.c', 'src/core/surface/client.c', 'src/core/surface/completion_queue.c', 'src/core/surface/event_string.c', 'src/core/surface/init.c', 'src/core/surface/lame_client.c', 'src/core/surface/metadata_array.c', 'src/core/surface/server.c', 'src/core/surface/server_chttp2.c', 'src/core/surface/server_create.c', 'src/core/surface/surface_trace.c', 'src/core/transport/chttp2/alpn.c', 'src/core/transport/chttp2/bin_encoder.c', 'src/core/transport/chttp2/frame_data.c', 'src/core/transport/chttp2/frame_goaway.c', 'src/core/transport/chttp2/frame_ping.c', 'src/core/transport/chttp2/frame_rst_stream.c', 'src/core/transport/chttp2/frame_settings.c', 'src/core/transport/chttp2/frame_window_update.c', 'src/core/transport/chttp2/hpack_parser.c', 'src/core/transport/chttp2/hpack_table.c', 'src/core/transport/chttp2/huffsyms.c', 'src/core/transport/chttp2/incoming_metadata.c', 'src/core/transport/chttp2/parsing.c', 'src/core/transport/chttp2/status_conversion.c', 'src/core/transport/chttp2/stream_encoder.c', 'src/core/transport/chttp2/stream_lists.c', 'src/core/transport/chttp2/stream_map.c', 'src/core/transport/chttp2/timeout_encoding.c', 'src/core/transport/chttp2/varint.c', 'src/core/transport/chttp2/writing.c', 'src/core/transport/chttp2_transport.c', 'src/core/transport/metadata.c', 'src/core/transport/stream_op.c', 'src/core/transport/transport.c', 'src/core/transport/transport_op_string.c', 'src/core/census/context.c', 'src/core/census/initialize.c', 
    cs.private_header_files = 'src/core/support/env.h', 'src/core/support/file.h', 'src/core/support/murmur_hash.h', 'src/core/support/string.h', 'src/core/support/string_win32.h', 'src/core/support/thd_internal.h', 'src/core/httpcli/format_request.h', 'src/core/httpcli/httpcli.h', 'src/core/httpcli/httpcli_security_connector.h', 'src/core/httpcli/parser.h', 'src/core/security/auth_filters.h', 'src/core/security/base64.h', 'src/core/security/credentials.h', 'src/core/security/json_token.h', 'src/core/security/secure_endpoint.h', 'src/core/security/secure_transport_setup.h', 'src/core/security/security_connector.h', 'src/core/security/security_context.h', 'src/core/tsi/fake_transport_security.h', 'src/core/tsi/ssl_transport_security.h', 'src/core/tsi/transport_security.h', 'src/core/tsi/transport_security_interface.h', 'src/core/census/grpc_context.h', 'src/core/channel/census_filter.h', 'src/core/channel/channel_args.h', 'src/core/channel/channel_stack.h', 'src/core/channel/child_channel.h', 'src/core/channel/client_channel.h', 'src/core/channel/client_setup.h', 'src/core/channel/connected_channel.h', 'src/core/channel/context.h', 'src/core/channel/http_client_filter.h', 'src/core/channel/http_server_filter.h', 'src/core/channel/noop_filter.h', 'src/core/compression/message_compress.h', 'src/core/debug/trace.h', 'src/core/iomgr/alarm.h', 'src/core/iomgr/alarm_heap.h', 'src/core/iomgr/alarm_internal.h', 'src/core/iomgr/endpoint.h', 'src/core/iomgr/endpoint_pair.h', 'src/core/iomgr/fd_posix.h', 'src/core/iomgr/iocp_windows.h', 'src/core/iomgr/iomgr.h', 'src/core/iomgr/iomgr_internal.h', 'src/core/iomgr/iomgr_posix.h', 'src/core/iomgr/pollset.h', 'src/core/iomgr/pollset_kick_posix.h', 'src/core/iomgr/pollset_posix.h', 'src/core/iomgr/pollset_set_posix.h', 'src/core/iomgr/pollset_set_windows.h', 'src/core/iomgr/pollset_windows.h', 'src/core/iomgr/resolve_address.h', 'src/core/iomgr/sockaddr.h', 'src/core/iomgr/sockaddr_posix.h', 'src/core/iomgr/sockaddr_utils.h', 'src/core/iomgr/sockaddr_win32.h', 'src/core/iomgr/socket_utils_posix.h', 'src/core/iomgr/socket_windows.h', 'src/core/iomgr/tcp_client.h', 'src/core/iomgr/tcp_posix.h', 'src/core/iomgr/tcp_server.h', 'src/core/iomgr/tcp_windows.h', 'src/core/iomgr/time_averaged_stats.h', 'src/core/iomgr/wakeup_fd_pipe.h', 'src/core/iomgr/wakeup_fd_posix.h', 'src/core/json/json.h', 'src/core/json/json_common.h', 'src/core/json/json_reader.h', 'src/core/json/json_writer.h', 'src/core/profiling/timers.h', 'src/core/profiling/timers_preciseclock.h', 'src/core/surface/byte_buffer_queue.h', 'src/core/surface/call.h', 'src/core/surface/channel.h', 'src/core/surface/client.h', 'src/core/surface/completion_queue.h', 'src/core/surface/event_string.h', 'src/core/surface/init.h', 'src/core/surface/server.h', 'src/core/surface/surface_trace.h', 'src/core/transport/chttp2/alpn.h', 'src/core/transport/chttp2/bin_encoder.h', 'src/core/transport/chttp2/frame.h', 'src/core/transport/chttp2/frame_data.h', 'src/core/transport/chttp2/frame_goaway.h', 'src/core/transport/chttp2/frame_ping.h', 'src/core/transport/chttp2/frame_rst_stream.h', 'src/core/transport/chttp2/frame_settings.h', 'src/core/transport/chttp2/frame_window_update.h', 'src/core/transport/chttp2/hpack_parser.h', 'src/core/transport/chttp2/hpack_table.h', 'src/core/transport/chttp2/http2_errors.h', 'src/core/transport/chttp2/huffsyms.h', 'src/core/transport/chttp2/incoming_metadata.h', 'src/core/transport/chttp2/internal.h', 'src/core/transport/chttp2/status_conversion.h', 'src/core/transport/chttp2/stream_encoder.h', 'src/core/transport/chttp2/stream_map.h', 'src/core/transport/chttp2/timeout_encoding.h', 'src/core/transport/chttp2/varint.h', 'src/core/transport/chttp2_transport.h', 'src/core/transport/metadata.h', 'src/core/transport/stream_op.h', 'src/core/transport/transport.h', 'src/core/transport/transport_impl.h', 'src/core/census/context.h', 
    cs.header_mappings_dir = '.'
    # The core library includes its headers as either "src/core/..." or "grpc/...", meaning we have
    # to tell XCode to look for headers under the "include" subdirectory too.
    #
    # TODO(jcanizales): Instead of doing this, during installation move everything under
    # "include/grpc" one directory up. The directory names under PODS_ROOT are implementation
    # details of Cocoapods, and have changed in the past, breaking this podspec.
    cs.xcconfig = { 'HEADER_SEARCH_PATHS' => '"$(PODS_ROOT)/Headers/Private/gRPC" ' +
                                             '"$(PODS_ROOT)/Headers/Private/gRPC/include"' }

    cs.requires_arc = false
    cs.libraries = 'z'
    cs.dependency 'OpenSSL', '~> 1.0.200'
  end

  # This is a workaround for Cocoapods Issue #1437.
  # It renames time.h and string.h to grpc_time.h and grpc_string.h.
  # It needs to be here (top-level) instead of in the C-Core subspec because Cocoapods doesn't run
  # prepare_command's of subspecs.
  #
  # TODO(jcanizales): Try out Todd Reed's solution at Issue #1437.
  s.prepare_command = <<-CMD
    DIR_TIME="grpc/support"
    BAD_TIME="$DIR_TIME/time.h"
    GOOD_TIME="$DIR_TIME/grpc_time.h"
    grep -rl "$BAD_TIME" include/grpc src/core | xargs sed -i '' -e s@$BAD_TIME@$GOOD_TIME@g
    if [ -f "include/$BAD_TIME" ];
    then
      mv -f "include/$BAD_TIME" "include/$GOOD_TIME"
    fi

    DIR_STRING="src/core/support"
    BAD_STRING="$DIR_STRING/string.h"
    GOOD_STRING="$DIR_STRING/grpc_string.h"
    grep -rl "$BAD_STRING" include/grpc src/core | xargs sed -i '' -e s@$BAD_STRING@$GOOD_STRING@g
    if [ -f "$BAD_STRING" ];
    then
      mv -f "$BAD_STRING" "$GOOD_STRING"
    fi
  CMD

  # Objective-C wrapper around the core gRPC library.
  s.subspec 'GRPCClient' do |gs|
    gs.source_files = 'src/objective-c/GRPCClient/*.{h,m}',
                      'src/objective-c/GRPCClient/private/*.{h,m}'
    gs.private_header_files = 'src/objective-c/GRPCClient/private/*.h'
    gs.compiler_flags = '-GCC_WARN_INHIBIT_ALL_WARNINGS', '-w'

    gs.dependency 'gRPC/C-Core'
    # TODO(jcanizales): Remove this when the prepare_command moves everything under "include/grpc"
    # one directory up.
    gs.xcconfig = { 'HEADER_SEARCH_PATHS' => '"$(PODS_ROOT)/Headers/Public/gRPC/include"' }
    gs.dependency 'gRPC/RxLibrary'

    # Certificates, to be able to establish TLS connections:
    gs.resource_bundles = { 'gRPC' => ['etc/roots.pem'] }
  end

  # RPC library for ProtocolBuffers, based on gRPC
  s.subspec 'ProtoRPC' do |ps|
    ps.source_files = 'src/objective-c/ProtoRPC/*.{h,m}'

    ps.dependency 'gRPC/GRPCClient'
    ps.dependency 'gRPC/RxLibrary'
    ps.dependency 'Protobuf', '~> 3.0.0-alpha-3'
  end
end
