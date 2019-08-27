# This file has been automatically generated from a template file.
# Please make modifications to `templates/gRPC-C++.podspec.template`
# instead. This file can be regenerated from the template by running
# `tools/buildgen/generate_projects.sh`.

# gRPC C++ CocoaPods podspec
#
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


Pod::Spec.new do |s|
  s.name     = 'gRPC-C++'
  # TODO (mxyan): use version that match gRPC version when pod is stabilized
  # version = '1.24.0-dev'
  version = '0.0.9-dev'
  s.version  = version
  s.summary  = 'gRPC C++ library'
  s.homepage = 'https://grpc.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'The gRPC contributors' => 'grpc-packages@google.com' }

  grpc_version = '1.24.0-dev'

  s.source = {
    :git => 'https://github.com/grpc/grpc.git',
    :tag => "v#{grpc_version}",
  }

  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'
  s.tvos.deployment_target = '10.0'

  s.requires_arc = false

  name = 'grpcpp'
  # Use `grpcpp` as framework name so that `#include <grpcpp/xxx.h>` works when built as
  # framework.
  s.module_name = name

  # Add include prefix `grpcpp` so that `#include <grpcpp/xxx.h>` works when built as static
  # library.
  s.header_dir = name

  s.pod_target_xcconfig = {
    'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(PODS_TARGET_SRCROOT)/include"',
    'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)"',
    'GCC_PREPROCESSOR_DEFINITIONS' => '"$(inherited)" "COCOAPODS=1"',
    'CLANG_WARN_STRICT_PROTOTYPES' => 'NO',
    'CLANG_WARN_DOCUMENTATION_COMMENTS' => 'NO',

    # If we don't set these two settings, `include/grpc/support/time.h` and
    # `src/core/lib/support/string.h` shadow the system `<time.h>` and `<string.h>`, breaking the
    # build.
    'USE_HEADERMAP' => 'NO',
    'ALWAYS_SEARCH_USER_PATHS' => 'NO',
  }

  s.libraries = 'c++'

  s.default_subspecs = 'Interface', 'Implementation'

  # Certificates, to be able to establish TLS connections:
  s.resource_bundles = { 'gRPCCertificates-Cpp' => ['etc/roots.pem'] }

  s.header_mappings_dir = 'include/grpcpp'

  s.subspec 'Interface' do |ss|
    ss.header_mappings_dir = 'include/grpcpp'

    ss.source_files = 'include/grpcpp/alarm.h',
                      'include/grpcpp/alarm_impl.h',
                      'include/grpcpp/channel.h',
                      'include/grpcpp/channel_impl.h',
                      'include/grpcpp/client_context.h',
                      'include/grpcpp/completion_queue.h',
                      'include/grpcpp/completion_queue_impl.h',
                      'include/grpcpp/create_channel.h',
                      'include/grpcpp/create_channel_impl.h',
                      'include/grpcpp/create_channel_posix.h',
                      'include/grpcpp/create_channel_posix_impl.h',
                      'include/grpcpp/ext/health_check_service_server_builder_option.h',
                      'include/grpcpp/generic/async_generic_service.h',
                      'include/grpcpp/generic/generic_stub.h',
                      'include/grpcpp/generic/generic_stub_impl.h',
                      'include/grpcpp/grpcpp.h',
                      'include/grpcpp/health_check_service_interface.h',
                      'include/grpcpp/health_check_service_interface_impl.h',
                      'include/grpcpp/impl/call.h',
                      'include/grpcpp/impl/channel_argument_option.h',
                      'include/grpcpp/impl/client_unary_call.h',
                      'include/grpcpp/impl/codegen/core_codegen.h',
                      'include/grpcpp/impl/grpc_library.h',
                      'include/grpcpp/impl/method_handler_impl.h',
                      'include/grpcpp/impl/rpc_method.h',
                      'include/grpcpp/impl/rpc_service_method.h',
                      'include/grpcpp/impl/serialization_traits.h',
                      'include/grpcpp/impl/server_builder_option.h',
                      'include/grpcpp/impl/server_builder_option_impl.h',
                      'include/grpcpp/impl/server_builder_plugin.h',
                      'include/grpcpp/impl/server_initializer.h',
                      'include/grpcpp/impl/server_initializer_impl.h',
                      'include/grpcpp/impl/service_type.h',
                      'include/grpcpp/resource_quota.h',
                      'include/grpcpp/resource_quota_impl.h',
                      'include/grpcpp/security/auth_context.h',
                      'include/grpcpp/security/auth_metadata_processor.h',
                      'include/grpcpp/security/auth_metadata_processor_impl.h',
                      'include/grpcpp/security/credentials.h',
                      'include/grpcpp/security/credentials_impl.h',
                      'include/grpcpp/security/server_credentials.h',
                      'include/grpcpp/security/server_credentials_impl.h',
                      'include/grpcpp/server.h',
                      'include/grpcpp/server_builder.h',
                      'include/grpcpp/server_builder_impl.h',
                      'include/grpcpp/server_context.h',
                      'include/grpcpp/server_impl.h',
                      'include/grpcpp/server_posix.h',
                      'include/grpcpp/server_posix_impl.h',
                      'include/grpcpp/support/async_stream.h',
                      'include/grpcpp/support/async_stream_impl.h',
                      'include/grpcpp/support/async_unary_call.h',
                      'include/grpcpp/support/async_unary_call_impl.h',
                      'include/grpcpp/support/byte_buffer.h',
                      'include/grpcpp/support/channel_arguments.h',
                      'include/grpcpp/support/channel_arguments_impl.h',
                      'include/grpcpp/support/client_callback.h',
                      'include/grpcpp/support/client_callback_impl.h',
                      'include/grpcpp/support/client_interceptor.h',
                      'include/grpcpp/support/config.h',
                      'include/grpcpp/support/interceptor.h',
                      'include/grpcpp/support/message_allocator.h',
                      'include/grpcpp/support/proto_buffer_reader.h',
                      'include/grpcpp/support/proto_buffer_writer.h',
                      'include/grpcpp/support/server_callback.h',
                      'include/grpcpp/support/server_callback_impl.h',
                      'include/grpcpp/support/server_interceptor.h',
                      'include/grpcpp/support/slice.h',
                      'include/grpcpp/support/status.h',
                      'include/grpcpp/support/status_code_enum.h',
                      'include/grpcpp/support/string_ref.h',
                      'include/grpcpp/support/stub_options.h',
                      'include/grpcpp/support/sync_stream.h',
                      'include/grpcpp/support/sync_stream_impl.h',
                      'include/grpcpp/support/time.h',
                      'include/grpcpp/support/validate_service_config.h',
                      'include/grpcpp/impl/codegen/async_generic_service.h',
                      'include/grpcpp/impl/codegen/async_stream.h',
                      'include/grpcpp/impl/codegen/async_stream_impl.h',
                      'include/grpcpp/impl/codegen/async_unary_call.h',
                      'include/grpcpp/impl/codegen/async_unary_call_impl.h',
                      'include/grpcpp/impl/codegen/byte_buffer.h',
                      'include/grpcpp/impl/codegen/call.h',
                      'include/grpcpp/impl/codegen/call_hook.h',
                      'include/grpcpp/impl/codegen/call_op_set.h',
                      'include/grpcpp/impl/codegen/call_op_set_interface.h',
                      'include/grpcpp/impl/codegen/callback_common.h',
                      'include/grpcpp/impl/codegen/channel_interface.h',
                      'include/grpcpp/impl/codegen/client_callback.h',
                      'include/grpcpp/impl/codegen/client_callback_impl.h',
                      'include/grpcpp/impl/codegen/client_context.h',
                      'include/grpcpp/impl/codegen/client_context_impl.h',
                      'include/grpcpp/impl/codegen/client_interceptor.h',
                      'include/grpcpp/impl/codegen/client_unary_call.h',
                      'include/grpcpp/impl/codegen/completion_queue.h',
                      'include/grpcpp/impl/codegen/completion_queue_impl.h',
                      'include/grpcpp/impl/codegen/completion_queue_tag.h',
                      'include/grpcpp/impl/codegen/config.h',
                      'include/grpcpp/impl/codegen/core_codegen_interface.h',
                      'include/grpcpp/impl/codegen/create_auth_context.h',
                      'include/grpcpp/impl/codegen/delegating_channel.h',
                      'include/grpcpp/impl/codegen/grpc_library.h',
                      'include/grpcpp/impl/codegen/intercepted_channel.h',
                      'include/grpcpp/impl/codegen/interceptor.h',
                      'include/grpcpp/impl/codegen/interceptor_common.h',
                      'include/grpcpp/impl/codegen/message_allocator.h',
                      'include/grpcpp/impl/codegen/metadata_map.h',
                      'include/grpcpp/impl/codegen/method_handler_impl.h',
                      'include/grpcpp/impl/codegen/rpc_method.h',
                      'include/grpcpp/impl/codegen/rpc_service_method.h',
                      'include/grpcpp/impl/codegen/security/auth_context.h',
                      'include/grpcpp/impl/codegen/serialization_traits.h',
                      'include/grpcpp/impl/codegen/server_callback.h',
                      'include/grpcpp/impl/codegen/server_callback_impl.h',
                      'include/grpcpp/impl/codegen/server_context.h',
                      'include/grpcpp/impl/codegen/server_context_impl.h',
                      'include/grpcpp/impl/codegen/server_interceptor.h',
                      'include/grpcpp/impl/codegen/server_interface.h',
                      'include/grpcpp/impl/codegen/service_type.h',
                      'include/grpcpp/impl/codegen/slice.h',
                      'include/grpcpp/impl/codegen/status.h',
                      'include/grpcpp/impl/codegen/status_code_enum.h',
                      'include/grpcpp/impl/codegen/string_ref.h',
                      'include/grpcpp/impl/codegen/stub_options.h',
                      'include/grpcpp/impl/codegen/sync_stream.h',
                      'include/grpcpp/impl/codegen/sync_stream_impl.h',
                      'include/grpcpp/impl/codegen/time.h',
                      'include/grpcpp/impl/codegen/sync.h',
                      'include/grpcpp/security/cronet_credentials.h',
                      'include/grpcpp/security/cronet_credentials_impl.h'
  end

  s.subspec 'Implementation' do |ss|
    ss.header_mappings_dir = '.'
    ss.dependency "#{s.name}/Interface", version
    ss.dependency 'gRPC-Core', grpc_version

    ss.source_files = 'include/grpcpp/impl/codegen/core_codegen.h',
                      'src/cpp/client/secure_credentials.h',
                      'src/cpp/common/secure_auth_context.h',
                      'src/cpp/server/secure_server_credentials.h',
                      'src/cpp/client/create_channel_internal.h',
                      'src/cpp/common/channel_filter.h',
                      'src/cpp/server/dynamic_thread_pool.h',
                      'src/cpp/server/external_connection_acceptor_impl.h',
                      'src/cpp/server/health/default_health_check_service.h',
                      'src/cpp/server/thread_pool_interface.h',
                      'src/cpp/thread_manager/thread_manager.h',
                      'src/cpp/client/insecure_credentials.cc',
                      'src/cpp/client/secure_credentials.cc',
                      'src/cpp/common/auth_property_iterator.cc',
                      'src/cpp/common/secure_auth_context.cc',
                      'src/cpp/common/secure_channel_arguments.cc',
                      'src/cpp/common/secure_create_auth_context.cc',
                      'src/cpp/server/insecure_server_credentials.cc',
                      'src/cpp/server/secure_server_credentials.cc',
                      'src/cpp/client/channel_cc.cc',
                      'src/cpp/client/client_context.cc',
                      'src/cpp/client/client_interceptor.cc',
                      'src/cpp/client/create_channel.cc',
                      'src/cpp/client/create_channel_internal.cc',
                      'src/cpp/client/create_channel_posix.cc',
                      'src/cpp/client/credentials_cc.cc',
                      'src/cpp/client/generic_stub.cc',
                      'src/cpp/common/alarm.cc',
                      'src/cpp/common/channel_arguments.cc',
                      'src/cpp/common/channel_filter.cc',
                      'src/cpp/common/completion_queue_cc.cc',
                      'src/cpp/common/core_codegen.cc',
                      'src/cpp/common/resource_quota_cc.cc',
                      'src/cpp/common/rpc_method.cc',
                      'src/cpp/common/validate_service_config.cc',
                      'src/cpp/common/version_cc.cc',
                      'src/cpp/server/async_generic_service.cc',
                      'src/cpp/server/channel_argument_option.cc',
                      'src/cpp/server/create_default_thread_pool.cc',
                      'src/cpp/server/dynamic_thread_pool.cc',
                      'src/cpp/server/external_connection_acceptor_impl.cc',
                      'src/cpp/server/health/default_health_check_service.cc',
                      'src/cpp/server/health/health_check_service.cc',
                      'src/cpp/server/health/health_check_service_server_builder_option.cc',
                      'src/cpp/server/server_builder.cc',
                      'src/cpp/server/server_cc.cc',
                      'src/cpp/server/server_context.cc',
                      'src/cpp/server/server_credentials.cc',
                      'src/cpp/server/server_posix.cc',
                      'src/cpp/thread_manager/thread_manager.cc',
                      'src/cpp/util/byte_buffer_cc.cc',
                      'src/cpp/util/status.cc',
                      'src/cpp/util/string_ref.cc',
                      'src/cpp/util/time_cc.cc',
                      'src/cpp/codegen/codegen_init.cc',
                      'src/cpp/client/cronet_credentials.cc'

    ss.private_header_files = 'include/grpcpp/impl/codegen/core_codegen.h',
                              'src/cpp/client/secure_credentials.h',
                              'src/cpp/common/secure_auth_context.h',
                              'src/cpp/server/secure_server_credentials.h',
                              'src/cpp/client/create_channel_internal.h',
                              'src/cpp/common/channel_filter.h',
                              'src/cpp/server/dynamic_thread_pool.h',
                              'src/cpp/server/external_connection_acceptor_impl.h',
                              'src/cpp/server/health/default_health_check_service.h',
                              'src/cpp/server/thread_pool_interface.h',
                              'src/cpp/thread_manager/thread_manager.h',
                              'src/core/lib/gpr/alloc.h',
                              'src/core/lib/gpr/arena.h',
                              'src/core/lib/gpr/env.h',
                              'src/core/lib/gpr/mpscq.h',
                              'src/core/lib/gpr/murmur_hash.h',
                              'src/core/lib/gpr/spinlock.h',
                              'src/core/lib/gpr/string.h',
                              'src/core/lib/gpr/string_windows.h',
                              'src/core/lib/gpr/time_precise.h',
                              'src/core/lib/gpr/tls.h',
                              'src/core/lib/gpr/tls_gcc.h',
                              'src/core/lib/gpr/tls_msvc.h',
                              'src/core/lib/gpr/tls_pthread.h',
                              'src/core/lib/gpr/tmpfile.h',
                              'src/core/lib/gpr/useful.h',
                              'src/core/lib/gprpp/abstract.h',
                              'src/core/lib/gprpp/arena.h',
                              'src/core/lib/gprpp/atomic.h',
                              'src/core/lib/gprpp/fork.h',
                              'src/core/lib/gprpp/global_config.h',
                              'src/core/lib/gprpp/global_config_custom.h',
                              'src/core/lib/gprpp/global_config_env.h',
                              'src/core/lib/gprpp/global_config_generic.h',
                              'src/core/lib/gprpp/host_port.h',
                              'src/core/lib/gprpp/manual_constructor.h',
                              'src/core/lib/gprpp/map.h',
                              'src/core/lib/gprpp/memory.h',
                              'src/core/lib/gprpp/pair.h',
                              'src/core/lib/gprpp/sync.h',
                              'src/core/lib/gprpp/thd.h',
                              'src/core/lib/profiling/timers.h',
                              'src/core/lib/avl/avl.h',
                              'src/core/lib/backoff/backoff.h',
                              'src/core/lib/channel/channel_args.h',
                              'src/core/lib/channel/channel_stack.h',
                              'src/core/lib/channel/channel_stack_builder.h',
                              'src/core/lib/channel/channel_trace.h',
                              'src/core/lib/channel/channelz.h',
                              'src/core/lib/channel/channelz_registry.h',
                              'src/core/lib/channel/connected_channel.h',
                              'src/core/lib/channel/context.h',
                              'src/core/lib/channel/handshaker.h',
                              'src/core/lib/channel/handshaker_factory.h',
                              'src/core/lib/channel/handshaker_registry.h',
                              'src/core/lib/channel/status_util.h',
                              'src/core/lib/compression/algorithm_metadata.h',
                              'src/core/lib/compression/compression_args.h',
                              'src/core/lib/compression/compression_internal.h',
                              'src/core/lib/compression/message_compress.h',
                              'src/core/lib/compression/stream_compression.h',
                              'src/core/lib/compression/stream_compression_gzip.h',
                              'src/core/lib/compression/stream_compression_identity.h',
                              'src/core/lib/debug/stats.h',
                              'src/core/lib/debug/stats_data.h',
                              'src/core/lib/gprpp/debug_location.h',
                              'src/core/lib/gprpp/inlined_vector.h',
                              'src/core/lib/gprpp/optional.h',
                              'src/core/lib/gprpp/orphanable.h',
                              'src/core/lib/gprpp/ref_counted.h',
                              'src/core/lib/gprpp/ref_counted_ptr.h',
                              'src/core/lib/gprpp/string_view.h',
                              'src/core/lib/http/format_request.h',
                              'src/core/lib/http/httpcli.h',
                              'src/core/lib/http/parser.h',
                              'src/core/lib/iomgr/block_annotate.h',
                              'src/core/lib/iomgr/buffer_list.h',
                              'src/core/lib/iomgr/call_combiner.h',
                              'src/core/lib/iomgr/cfstream_handle.h',
                              'src/core/lib/iomgr/closure.h',
                              'src/core/lib/iomgr/combiner.h',
                              'src/core/lib/iomgr/dynamic_annotations.h',
                              'src/core/lib/iomgr/endpoint.h',
                              'src/core/lib/iomgr/endpoint_cfstream.h',
                              'src/core/lib/iomgr/endpoint_pair.h',
                              'src/core/lib/iomgr/error.h',
                              'src/core/lib/iomgr/error_cfstream.h',
                              'src/core/lib/iomgr/error_internal.h',
                              'src/core/lib/iomgr/ev_epoll1_linux.h',
                              'src/core/lib/iomgr/ev_epollex_linux.h',
                              'src/core/lib/iomgr/ev_poll_posix.h',
                              'src/core/lib/iomgr/ev_posix.h',
                              'src/core/lib/iomgr/exec_ctx.h',
                              'src/core/lib/iomgr/executor.h',
                              'src/core/lib/iomgr/executor/mpmcqueue.h',
                              'src/core/lib/iomgr/executor/threadpool.h',
                              'src/core/lib/iomgr/gethostname.h',
                              'src/core/lib/iomgr/grpc_if_nametoindex.h',
                              'src/core/lib/iomgr/internal_errqueue.h',
                              'src/core/lib/iomgr/iocp_windows.h',
                              'src/core/lib/iomgr/iomgr.h',
                              'src/core/lib/iomgr/iomgr_custom.h',
                              'src/core/lib/iomgr/iomgr_internal.h',
                              'src/core/lib/iomgr/iomgr_posix.h',
                              'src/core/lib/iomgr/is_epollexclusive_available.h',
                              'src/core/lib/iomgr/load_file.h',
                              'src/core/lib/iomgr/lockfree_event.h',
                              'src/core/lib/iomgr/nameser.h',
                              'src/core/lib/iomgr/polling_entity.h',
                              'src/core/lib/iomgr/pollset.h',
                              'src/core/lib/iomgr/pollset_custom.h',
                              'src/core/lib/iomgr/pollset_set.h',
                              'src/core/lib/iomgr/pollset_set_custom.h',
                              'src/core/lib/iomgr/pollset_set_windows.h',
                              'src/core/lib/iomgr/pollset_windows.h',
                              'src/core/lib/iomgr/port.h',
                              'src/core/lib/iomgr/resolve_address.h',
                              'src/core/lib/iomgr/resolve_address_custom.h',
                              'src/core/lib/iomgr/resource_quota.h',
                              'src/core/lib/iomgr/sockaddr.h',
                              'src/core/lib/iomgr/sockaddr_custom.h',
                              'src/core/lib/iomgr/sockaddr_posix.h',
                              'src/core/lib/iomgr/sockaddr_utils.h',
                              'src/core/lib/iomgr/sockaddr_windows.h',
                              'src/core/lib/iomgr/socket_factory_posix.h',
                              'src/core/lib/iomgr/socket_mutator.h',
                              'src/core/lib/iomgr/socket_utils.h',
                              'src/core/lib/iomgr/socket_utils_posix.h',
                              'src/core/lib/iomgr/socket_windows.h',
                              'src/core/lib/iomgr/sys_epoll_wrapper.h',
                              'src/core/lib/iomgr/tcp_client.h',
                              'src/core/lib/iomgr/tcp_client_posix.h',
                              'src/core/lib/iomgr/tcp_custom.h',
                              'src/core/lib/iomgr/tcp_posix.h',
                              'src/core/lib/iomgr/tcp_server.h',
                              'src/core/lib/iomgr/tcp_server_utils_posix.h',
                              'src/core/lib/iomgr/tcp_windows.h',
                              'src/core/lib/iomgr/time_averaged_stats.h',
                              'src/core/lib/iomgr/timer.h',
                              'src/core/lib/iomgr/timer_custom.h',
                              'src/core/lib/iomgr/timer_heap.h',
                              'src/core/lib/iomgr/timer_manager.h',
                              'src/core/lib/iomgr/udp_server.h',
                              'src/core/lib/iomgr/unix_sockets_posix.h',
                              'src/core/lib/iomgr/wakeup_fd_pipe.h',
                              'src/core/lib/iomgr/wakeup_fd_posix.h',
                              'src/core/lib/json/json.h',
                              'src/core/lib/json/json_common.h',
                              'src/core/lib/json/json_reader.h',
                              'src/core/lib/json/json_writer.h',
                              'src/core/lib/slice/b64.h',
                              'src/core/lib/slice/percent_encoding.h',
                              'src/core/lib/slice/slice_hash_table.h',
                              'src/core/lib/slice/slice_internal.h',
                              'src/core/lib/slice/slice_string_helpers.h',
                              'src/core/lib/slice/slice_utils.h',
                              'src/core/lib/slice/slice_weak_hash_table.h',
                              'src/core/lib/surface/api_trace.h',
                              'src/core/lib/surface/call.h',
                              'src/core/lib/surface/call_test_only.h',
                              'src/core/lib/surface/channel.h',
                              'src/core/lib/surface/channel_init.h',
                              'src/core/lib/surface/channel_stack_type.h',
                              'src/core/lib/surface/completion_queue.h',
                              'src/core/lib/surface/completion_queue_factory.h',
                              'src/core/lib/surface/event_string.h',
                              'src/core/lib/surface/init.h',
                              'src/core/lib/surface/lame_client.h',
                              'src/core/lib/surface/server.h',
                              'src/core/lib/surface/validate_metadata.h',
                              'src/core/lib/transport/bdp_estimator.h',
                              'src/core/lib/transport/byte_stream.h',
                              'src/core/lib/transport/connectivity_state.h',
                              'src/core/lib/transport/error_utils.h',
                              'src/core/lib/transport/http2_errors.h',
                              'src/core/lib/transport/metadata.h',
                              'src/core/lib/transport/metadata_batch.h',
                              'src/core/lib/transport/pid_controller.h',
                              'src/core/lib/transport/static_metadata.h',
                              'src/core/lib/transport/status_conversion.h',
                              'src/core/lib/transport/status_metadata.h',
                              'src/core/lib/transport/timeout_encoding.h',
                              'src/core/lib/transport/transport.h',
                              'src/core/lib/transport/transport_impl.h',
                              'src/core/lib/uri/uri_parser.h',
                              'src/core/lib/debug/trace.h',
                              'src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.h',
                              'third_party/upb/upb/decode.h',
                              'third_party/upb/upb/encode.h',
                              'third_party/upb/upb/generated_util.h',
                              'third_party/upb/upb/msg.h',
                              'third_party/upb/upb/port_def.inc',
                              'third_party/upb/upb/port_undef.inc',
                              'third_party/upb/upb/table.int.h',
                              'third_party/upb/upb/upb.h',
                              'src/core/ext/transport/inproc/inproc_transport.h'
  end

  s.subspec 'Protobuf' do |ss|
    ss.header_mappings_dir = 'include/grpcpp'
    ss.dependency "#{s.name}/Interface", version

    ss.source_files = 'include/grpcpp/impl/codegen/proto_buffer_reader.h',
                      'include/grpcpp/impl/codegen/proto_buffer_writer.h',
                      'include/grpcpp/impl/codegen/proto_utils.h',
                      'include/grpcpp/impl/codegen/config_protobuf.h',
                      'include/grpcpp/impl/codegen/config_protobuf.h'
  end

  s.prepare_command = <<-END_OF_COMMAND
    find src/core/ third_party/upb/ -type f \\( -name '*.h' -or -name '*.c' -or -name '*.cc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include "upb/(.*)";#if COCOAPODS==1\\\n  #include  "third_party/upb/upb/\\1"\\\n#else\\\n  #include  "upb/\\1"\\\n#endif;g'
    find src/core/ third_party/upb/ -type f -name '*.grpc_back' -print0 | xargs -0 rm
    find src/core/ src/cpp/ -type f \\( -name '*.h' -or -name '*.c' -or -name '*.cc' \\) -print0 | xargs -0 -L1 sed -E -i'.grpc_back' 's;#include "(.*).upb.h";#if COCOAPODS==1\\\n  #include  "src/core/ext/upb-generated/\\1.upb.h"\\\n#else\\\n  #include  "\\1.upb.h"\\\n#endif;g'
    find src/core/ src/cpp/ -type f -name '*.grpc_back' -print0 | xargs -0 rm
  END_OF_COMMAND
end
