# gRPC Bazel BUILD file.
#
# Copyright 2016 gRPC authors.
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

licenses(["notice"])  # Apache v2

exports_files(["LICENSE"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_proto_plugin",
    "grpc_generate_one_off_targets",
)

# This should be updated along with build.yaml
g_stands_for = "gregarious"

core_version = "4.0.0-dev"

version = "1.5.0-dev"

GPR_PUBLIC_HDRS = [
    "include/grpc/support/alloc.h",
    "include/grpc/support/atm.h",
    "include/grpc/support/atm_gcc_atomic.h",
    "include/grpc/support/atm_gcc_sync.h",
    "include/grpc/support/atm_windows.h",
    "include/grpc/support/avl.h",
    "include/grpc/support/cmdline.h",
    "include/grpc/support/cpu.h",
    "include/grpc/support/histogram.h",
    "include/grpc/support/host_port.h",
    "include/grpc/support/log.h",
    "include/grpc/support/log_windows.h",
    "include/grpc/support/port_platform.h",
    "include/grpc/support/string_util.h",
    "include/grpc/support/subprocess.h",
    "include/grpc/support/sync.h",
    "include/grpc/support/sync_generic.h",
    "include/grpc/support/sync_posix.h",
    "include/grpc/support/sync_windows.h",
    "include/grpc/support/thd.h",
    "include/grpc/support/time.h",
    "include/grpc/support/tls.h",
    "include/grpc/support/tls_gcc.h",
    "include/grpc/support/tls_msvc.h",
    "include/grpc/support/tls_pthread.h",
    "include/grpc/support/useful.h",
]

GRPC_PUBLIC_HDRS = [
    "include/grpc/byte_buffer.h",
    "include/grpc/byte_buffer_reader.h",
    "include/grpc/compression.h",
    "include/grpc/load_reporting.h",
    "include/grpc/grpc.h",
    "include/grpc/grpc_posix.h",
    "include/grpc/grpc_security_constants.h",
    "include/grpc/slice.h",
    "include/grpc/slice_buffer.h",
    "include/grpc/status.h",
    "include/grpc/support/workaround_list.h",
]

GRPC_SECURE_PUBLIC_HDRS = [
    "include/grpc/grpc_security.h",
]

# TODO(ctiller): layer grpc atop grpc_unsecure, layer grpc++ atop grpc++_unsecure
GRPCXX_SRCS = [
    "src/cpp/client/channel_cc.cc",
    "src/cpp/client/client_context.cc",
    "src/cpp/client/create_channel.cc",
    "src/cpp/client/create_channel_internal.cc",
    "src/cpp/client/create_channel_posix.cc",
    "src/cpp/client/credentials_cc.cc",
    "src/cpp/client/generic_stub.cc",
    "src/cpp/common/channel_arguments.cc",
    "src/cpp/common/channel_filter.cc",
    "src/cpp/common/completion_queue_cc.cc",
    "src/cpp/common/core_codegen.cc",
    "src/cpp/common/resource_quota_cc.cc",
    "src/cpp/common/rpc_method.cc",
    "src/cpp/common/version_cc.cc",
    "src/cpp/server/async_generic_service.cc",
    "src/cpp/server/channel_argument_option.cc",
    "src/cpp/server/create_default_thread_pool.cc",
    "src/cpp/server/dynamic_thread_pool.cc",
    "src/cpp/server/health/default_health_check_service.cc",
    "src/cpp/server/health/health.pb.c",
    "src/cpp/server/health/health_check_service.cc",
    "src/cpp/server/health/health_check_service_server_builder_option.cc",
    "src/cpp/server/server_builder.cc",
    "src/cpp/server/server_cc.cc",
    "src/cpp/server/server_context.cc",
    "src/cpp/server/server_credentials.cc",
    "src/cpp/server/server_posix.cc",
    "src/cpp/thread_manager/thread_manager.cc",
    "src/cpp/util/byte_buffer_cc.cc",
    "src/cpp/util/slice_cc.cc",
    "src/cpp/util/status.cc",
    "src/cpp/util/string_ref.cc",
    "src/cpp/util/time_cc.cc",
]

GRPCXX_HDRS = [
    "src/cpp/client/create_channel_internal.h",
    "src/cpp/common/channel_filter.h",
    "src/cpp/server/dynamic_thread_pool.h",
    "src/cpp/server/health/default_health_check_service.h",
    "src/cpp/server/health/health.pb.h",
    "src/cpp/server/thread_pool_interface.h",
    "src/cpp/thread_manager/thread_manager.h",
]

GRPCXX_PUBLIC_HDRS = [
    "include/grpc++/alarm.h",
    "include/grpc++/channel.h",
    "include/grpc++/client_context.h",
    "include/grpc++/completion_queue.h",
    "include/grpc++/create_channel.h",
    "include/grpc++/create_channel_posix.h",
    "include/grpc++/ext/health_check_service_server_builder_option.h",
    "include/grpc++/generic/async_generic_service.h",
    "include/grpc++/generic/generic_stub.h",
    "include/grpc++/grpc++.h",
    "include/grpc++/health_check_service_interface.h",
    "include/grpc++/impl/call.h",
    "include/grpc++/impl/channel_argument_option.h",
    "include/grpc++/impl/client_unary_call.h",
    "include/grpc++/impl/codegen/core_codegen.h",
    "include/grpc++/impl/grpc_library.h",
    "include/grpc++/impl/method_handler_impl.h",
    "include/grpc++/impl/rpc_method.h",
    "include/grpc++/impl/rpc_service_method.h",
    "include/grpc++/impl/serialization_traits.h",
    "include/grpc++/impl/server_builder_option.h",
    "include/grpc++/impl/server_builder_plugin.h",
    "include/grpc++/impl/server_initializer.h",
    "include/grpc++/impl/service_type.h",
    "include/grpc++/impl/sync_cxx11.h",
    "include/grpc++/impl/sync_no_cxx11.h",
    "include/grpc++/resource_quota.h",
    "include/grpc++/security/auth_context.h",
    "include/grpc++/security/auth_metadata_processor.h",
    "include/grpc++/security/credentials.h",
    "include/grpc++/security/server_credentials.h",
    "include/grpc++/server.h",
    "include/grpc++/server_builder.h",
    "include/grpc++/server_context.h",
    "include/grpc++/server_posix.h",
    "include/grpc++/support/async_stream.h",
    "include/grpc++/support/async_unary_call.h",
    "include/grpc++/support/byte_buffer.h",
    "include/grpc++/support/channel_arguments.h",
    "include/grpc++/support/config.h",
    "include/grpc++/support/slice.h",
    "include/grpc++/support/status.h",
    "include/grpc++/support/status_code_enum.h",
    "include/grpc++/support/string_ref.h",
    "include/grpc++/support/stub_options.h",
    "include/grpc++/support/sync_stream.h",
    "include/grpc++/support/time.h",
]

grpc_cc_library(
    name = "gpr",
    language = "c",
    public_hdrs = GPR_PUBLIC_HDRS,
    standalone = True,
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "grpc_unsecure",
    srcs = [
        "src/core/lib/surface/init.c",
        "src/core/lib/surface/init_unsecure.c",
        "src/core/plugin_registry/grpc_unsecure_plugin_registry.c",
    ],
    language = "c",
    public_hdrs = GRPC_PUBLIC_HDRS,
    standalone = True,
    deps = [
        "grpc_common",
        "grpc_lb_policy_grpclb",
    ],
)

grpc_cc_library(
    name = "grpc",
    srcs = [
        "src/core/lib/surface/init.c",
        "src/core/plugin_registry/grpc_plugin_registry.c",
    ],
    language = "c",
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_SECURE_PUBLIC_HDRS,
    standalone = True,
    deps = [
        "grpc_common",
        "grpc_lb_policy_grpclb_secure",
        "grpc_secure",
        "grpc_transport_chttp2_client_secure",
        "grpc_transport_chttp2_server_secure",
    ],
)

grpc_cc_library(
    name = "grpc_cronet",
    srcs = [
        "src/core/lib/surface/init.c",
        "src/core/plugin_registry/grpc_cronet_plugin_registry.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_http_filters",
        "grpc_transport_chttp2_client_secure",
        "grpc_transport_cronet_client_secure",
    ],
)

grpc_cc_library(
    name = "grpc++",
    srcs = [
        "src/cpp/client/insecure_credentials.cc",
        "src/cpp/client/secure_credentials.cc",
        "src/cpp/common/auth_property_iterator.cc",
        "src/cpp/common/secure_auth_context.cc",
        "src/cpp/common/secure_channel_arguments.cc",
        "src/cpp/common/secure_create_auth_context.cc",
        "src/cpp/server/insecure_server_credentials.cc",
        "src/cpp/server/secure_server_credentials.cc",
    ],
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    standalone = True,
    deps = [
        "gpr",
        "grpc",
        "grpc++_base",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
    ],
)

grpc_cc_library(
    name = "grpc++_unsecure",
    srcs = [
        "src/cpp/client/insecure_credentials.cc",
        "src/cpp/common/insecure_create_auth_context.cc",
        "src/cpp/server/insecure_server_credentials.cc",
    ],
    language = "c++",
    standalone = True,
    deps = [
        "gpr",
        "grpc++_base_unsecure",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
        "grpc_unsecure",
    ],
)

grpc_cc_library(
    name = "grpc++_error_details",
    srcs = [
        "src/cpp/util/error_details.cc",
    ],
    hdrs = [
        "include/grpc++/support/error_details.h",
    ],
    language = "c++",
    standalone = True,
    deps = [
        "grpc++",
        "//src/proto/grpc/status:status_proto",
    ],
)

grpc_cc_library(
    name = "grpc_plugin_support",
    srcs = [
        "src/compiler/cpp_generator.cc",
        "src/compiler/csharp_generator.cc",
        "src/compiler/node_generator.cc",
        "src/compiler/objective_c_generator.cc",
        "src/compiler/php_generator.cc",
        "src/compiler/python_generator.cc",
        "src/compiler/ruby_generator.cc",
    ],
    hdrs = [
        "src/compiler/config.h",
        "src/compiler/cpp_generator.h",
        "src/compiler/cpp_generator_helpers.h",
        "src/compiler/csharp_generator.h",
        "src/compiler/csharp_generator_helpers.h",
        "src/compiler/generator_helpers.h",
        "src/compiler/node_generator.h",
        "src/compiler/node_generator_helpers.h",
        "src/compiler/objective_c_generator.h",
        "src/compiler/objective_c_generator_helpers.h",
        "src/compiler/php_generator.h",
        "src/compiler/php_generator_helpers.h",
        "src/compiler/protobuf_plugin.h",
        "src/compiler/python_generator.h",
        "src/compiler/python_generator_helpers.h",
        "src/compiler/python_private_generator.h",
        "src/compiler/ruby_generator.h",
        "src/compiler/ruby_generator_helpers-inl.h",
        "src/compiler/ruby_generator_map-inl.h",
        "src/compiler/ruby_generator_string-inl.h",
        "src/compiler/schema_interface.h",
    ],
    external_deps = [
        "protobuf_clib",
    ],
    language = "c++",
    deps = [
        "grpc++_config_proto",
    ],
)

grpc_proto_plugin(
    name = "grpc_cpp_plugin",
    srcs = ["src/compiler/cpp_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_csharp_plugin",
    srcs = ["src/compiler/csharp_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_node_plugin",
    srcs = ["src/compiler/node_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_objective_c_plugin",
    srcs = ["src/compiler/objective_c_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_php_plugin",
    srcs = ["src/compiler/php_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_python_plugin",
    srcs = ["src/compiler/python_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_ruby_plugin",
    srcs = ["src/compiler/ruby_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_cc_library(
    name = "grpc_csharp_ext",
    srcs = [
        "src/csharp/ext/grpc_csharp_ext.c",
    ],
    language = "csharp",
    deps = [
        "gpr",
        "grpc",
    ],
)

grpc_cc_library(
    name = "census",
    srcs = [
        "src/core/ext/census/base_resources.c",
        "src/core/ext/census/context.c",
        "src/core/ext/census/gen/census.pb.c",
        "src/core/ext/census/gen/trace_context.pb.c",
        "src/core/ext/census/grpc_context.c",
        "src/core/ext/census/grpc_filter.c",
        "src/core/ext/census/grpc_plugin.c",
        "src/core/ext/census/initialize.c",
        "src/core/ext/census/intrusive_hash_map.c",
        "src/core/ext/census/mlog.c",
        "src/core/ext/census/operation.c",
        "src/core/ext/census/placeholders.c",
        "src/core/ext/census/resource.c",
        "src/core/ext/census/trace_context.c",
        "src/core/ext/census/tracing.c",
    ],
    hdrs = [
        "src/core/ext/census/aggregation.h",
        "src/core/ext/census/base_resources.h",
        "src/core/ext/census/census_interface.h",
        "src/core/ext/census/census_rpc_stats.h",
        "src/core/ext/census/gen/census.pb.h",
        "src/core/ext/census/gen/trace_context.pb.h",
        "src/core/ext/census/grpc_filter.h",
        "src/core/ext/census/intrusive_hash_map.h",
        "src/core/ext/census/intrusive_hash_map_internal.h",
        "src/core/ext/census/mlog.h",
        "src/core/ext/census/resource.h",
        "src/core/ext/census/rpc_metric_id.h",
        "src/core/ext/census/trace_context.h",
        "src/core/ext/census/trace_label.h",
        "src/core/ext/census/trace_propagation.h",
        "src/core/ext/census/trace_status.h",
        "src/core/ext/census/trace_string.h",
        "src/core/ext/census/tracing.h",
    ],
    external_deps = [
        "nanopb",
        "libssl",
    ],
    language = "c",
    public_hdrs = [
        "include/grpc/census.h",
    ],
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "gpr_base",
    srcs = [
        "src/core/lib/profiling/basic_timers.c",
        "src/core/lib/profiling/stap_timers.c",
        "src/core/lib/support/alloc.c",
        "src/core/lib/support/arena.c",
        "src/core/lib/support/atm.c",
        "src/core/lib/support/avl.c",
        "src/core/lib/support/backoff.c",
        "src/core/lib/support/cmdline.c",
        "src/core/lib/support/cpu_iphone.c",
        "src/core/lib/support/cpu_linux.c",
        "src/core/lib/support/cpu_posix.c",
        "src/core/lib/support/cpu_windows.c",
        "src/core/lib/support/env_linux.c",
        "src/core/lib/support/env_posix.c",
        "src/core/lib/support/env_windows.c",
        "src/core/lib/support/histogram.c",
        "src/core/lib/support/host_port.c",
        "src/core/lib/support/log.c",
        "src/core/lib/support/log_android.c",
        "src/core/lib/support/log_linux.c",
        "src/core/lib/support/log_posix.c",
        "src/core/lib/support/log_windows.c",
        "src/core/lib/support/mpscq.c",
        "src/core/lib/support/murmur_hash.c",
        "src/core/lib/support/stack_lockfree.c",
        "src/core/lib/support/string.c",
        "src/core/lib/support/string_posix.c",
        "src/core/lib/support/string_util_windows.c",
        "src/core/lib/support/string_windows.c",
        "src/core/lib/support/subprocess_posix.c",
        "src/core/lib/support/subprocess_windows.c",
        "src/core/lib/support/sync.c",
        "src/core/lib/support/sync_posix.c",
        "src/core/lib/support/sync_windows.c",
        "src/core/lib/support/thd.c",
        "src/core/lib/support/thd_posix.c",
        "src/core/lib/support/thd_windows.c",
        "src/core/lib/support/time.c",
        "src/core/lib/support/time_posix.c",
        "src/core/lib/support/time_precise.c",
        "src/core/lib/support/time_windows.c",
        "src/core/lib/support/tls_pthread.c",
        "src/core/lib/support/tmpfile_msys.c",
        "src/core/lib/support/tmpfile_posix.c",
        "src/core/lib/support/tmpfile_windows.c",
        "src/core/lib/support/wrap_memcpy.c",
    ],
    hdrs = [
        "src/core/lib/profiling/timers.h",
        "src/core/lib/support/arena.h",
        "src/core/lib/support/atomic.h",
        "src/core/lib/support/atomic_with_atm.h",
        "src/core/lib/support/atomic_with_std.h",
        "src/core/lib/support/backoff.h",
        "src/core/lib/support/block_annotate.h",
        "src/core/lib/support/env.h",
        "src/core/lib/support/memory.h",
        "src/core/lib/support/mpscq.h",
        "src/core/lib/support/murmur_hash.h",
        "src/core/lib/support/spinlock.h",
        "src/core/lib/support/stack_lockfree.h",
        "src/core/lib/support/string.h",
        "src/core/lib/support/string_windows.h",
        "src/core/lib/support/thd_internal.h",
        "src/core/lib/support/time_precise.h",
        "src/core/lib/support/tmpfile.h",
    ],
    language = "c",
    public_hdrs = GPR_PUBLIC_HDRS,
    deps = [
        "gpr_codegen",
    ],
)

grpc_cc_library(
    name = "gpr_codegen",
    language = "c",
    public_hdrs = [
        "include/grpc/impl/codegen/atm.h",
        "include/grpc/impl/codegen/atm_gcc_atomic.h",
        "include/grpc/impl/codegen/atm_gcc_sync.h",
        "include/grpc/impl/codegen/atm_windows.h",
        "include/grpc/impl/codegen/gpr_slice.h",
        "include/grpc/impl/codegen/gpr_types.h",
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/impl/codegen/sync.h",
        "include/grpc/impl/codegen/sync_generic.h",
        "include/grpc/impl/codegen/sync_posix.h",
        "include/grpc/impl/codegen/sync_windows.h",
    ],
)

grpc_cc_library(
    name = "grpc_trace",
    srcs = ["src/core/lib/debug/trace.c"],
    hdrs = ["src/core/lib/debug/trace.h"],
    language = "c",
    deps = [":gpr"],
)

grpc_cc_library(
    name = "grpc_base_c",
    srcs = [
        "src/core/lib/channel/channel_args.c",
        "src/core/lib/channel/channel_stack.c",
        "src/core/lib/channel/channel_stack_builder.c",
        "src/core/lib/channel/connected_channel.c",
        "src/core/lib/channel/handshaker.c",
        "src/core/lib/channel/handshaker_factory.c",
        "src/core/lib/channel/handshaker_registry.c",
        "src/core/lib/compression/compression.c",
        "src/core/lib/compression/message_compress.c",
        "src/core/lib/compression/stream_compression.c",
        "src/core/lib/http/format_request.c",
        "src/core/lib/http/httpcli.c",
        "src/core/lib/http/parser.c",
        "src/core/lib/iomgr/closure.c",
        "src/core/lib/iomgr/combiner.c",
        "src/core/lib/iomgr/endpoint.c",
        "src/core/lib/iomgr/endpoint_pair_posix.c",
        "src/core/lib/iomgr/endpoint_pair_uv.c",
        "src/core/lib/iomgr/endpoint_pair_windows.c",
        "src/core/lib/iomgr/error.c",
        "src/core/lib/iomgr/ev_epoll1_linux.c",
        "src/core/lib/iomgr/ev_epoll_limited_pollers_linux.c",
        "src/core/lib/iomgr/ev_epoll_thread_pool_linux.c",
        "src/core/lib/iomgr/ev_epollex_linux.c",
        "src/core/lib/iomgr/ev_epollsig_linux.c",
        "src/core/lib/iomgr/ev_poll_posix.c",
        "src/core/lib/iomgr/ev_posix.c",
        "src/core/lib/iomgr/ev_windows.c",
        "src/core/lib/iomgr/exec_ctx.c",
        "src/core/lib/iomgr/executor.c",
        "src/core/lib/iomgr/gethostname_host_name_max.c",
        "src/core/lib/iomgr/gethostname_sysconf.c",
        "src/core/lib/iomgr/gethostname_fallback.c",
        "src/core/lib/iomgr/iocp_windows.c",
        "src/core/lib/iomgr/iomgr.c",
        "src/core/lib/iomgr/iomgr_posix.c",
        "src/core/lib/iomgr/iomgr_uv.c",
        "src/core/lib/iomgr/iomgr_windows.c",
        "src/core/lib/iomgr/is_epollexclusive_available.c",
        "src/core/lib/iomgr/load_file.c",
        "src/core/lib/iomgr/lockfree_event.c",
        "src/core/lib/iomgr/network_status_tracker.c",
        "src/core/lib/iomgr/polling_entity.c",
        "src/core/lib/iomgr/pollset_set_uv.c",
        "src/core/lib/iomgr/pollset_set_windows.c",
        "src/core/lib/iomgr/pollset_uv.c",
        "src/core/lib/iomgr/pollset_windows.c",
        "src/core/lib/iomgr/resolve_address_posix.c",
        "src/core/lib/iomgr/resolve_address_uv.c",
        "src/core/lib/iomgr/resolve_address_windows.c",
        "src/core/lib/iomgr/resource_quota.c",
        "src/core/lib/iomgr/sockaddr_utils.c",
        "src/core/lib/iomgr/socket_factory_posix.c",
        "src/core/lib/iomgr/socket_mutator.c",
        "src/core/lib/iomgr/socket_utils_common_posix.c",
        "src/core/lib/iomgr/socket_utils_linux.c",
        "src/core/lib/iomgr/socket_utils_posix.c",
        "src/core/lib/iomgr/socket_utils_uv.c",
        "src/core/lib/iomgr/socket_utils_windows.c",
        "src/core/lib/iomgr/socket_windows.c",
        "src/core/lib/iomgr/tcp_client_posix.c",
        "src/core/lib/iomgr/tcp_client_uv.c",
        "src/core/lib/iomgr/tcp_client_windows.c",
        "src/core/lib/iomgr/tcp_posix.c",
        "src/core/lib/iomgr/tcp_server_posix.c",
        "src/core/lib/iomgr/tcp_server_utils_posix_common.c",
        "src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.c",
        "src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.c",
        "src/core/lib/iomgr/tcp_server_uv.c",
        "src/core/lib/iomgr/tcp_server_windows.c",
        "src/core/lib/iomgr/tcp_uv.c",
        "src/core/lib/iomgr/tcp_windows.c",
        "src/core/lib/iomgr/time_averaged_stats.c",
        "src/core/lib/iomgr/timer_generic.c",
        "src/core/lib/iomgr/timer_heap.c",
        "src/core/lib/iomgr/timer_manager.c",
        "src/core/lib/iomgr/timer_uv.c",
        "src/core/lib/iomgr/udp_server.c",
        "src/core/lib/iomgr/unix_sockets_posix.c",
        "src/core/lib/iomgr/unix_sockets_posix_noop.c",
        "src/core/lib/iomgr/wakeup_fd_cv.c",
        "src/core/lib/iomgr/wakeup_fd_eventfd.c",
        "src/core/lib/iomgr/wakeup_fd_nospecial.c",
        "src/core/lib/iomgr/wakeup_fd_pipe.c",
        "src/core/lib/iomgr/wakeup_fd_posix.c",
        "src/core/lib/json/json.c",
        "src/core/lib/json/json_reader.c",
        "src/core/lib/json/json_string.c",
        "src/core/lib/json/json_writer.c",
        "src/core/lib/slice/b64.c",
        "src/core/lib/slice/percent_encoding.c",
        "src/core/lib/slice/slice.c",
        "src/core/lib/slice/slice_buffer.c",
        "src/core/lib/slice/slice_hash_table.c",
        "src/core/lib/slice/slice_intern.c",
        "src/core/lib/slice/slice_string_helpers.c",
        "src/core/lib/surface/alarm.c",
        "src/core/lib/surface/api_trace.c",
        "src/core/lib/surface/byte_buffer.c",
        "src/core/lib/surface/byte_buffer_reader.c",
        "src/core/lib/surface/call.c",
        "src/core/lib/surface/call_details.c",
        "src/core/lib/surface/call_log_batch.c",
        "src/core/lib/surface/channel.c",
        "src/core/lib/surface/channel_init.c",
        "src/core/lib/surface/channel_ping.c",
        "src/core/lib/surface/channel_stack_type.c",
        "src/core/lib/surface/completion_queue.c",
        "src/core/lib/surface/completion_queue_factory.c",
        "src/core/lib/surface/event_string.c",
        "src/core/lib/surface/metadata_array.c",
        "src/core/lib/surface/server.c",
        "src/core/lib/surface/validate_metadata.c",
        "src/core/lib/surface/version.c",
        "src/core/lib/transport/bdp_estimator.c",
        "src/core/lib/transport/byte_stream.c",
        "src/core/lib/transport/connectivity_state.c",
        "src/core/lib/transport/error_utils.c",
        "src/core/lib/transport/metadata.c",
        "src/core/lib/transport/metadata_batch.c",
        "src/core/lib/transport/pid_controller.c",
        "src/core/lib/transport/service_config.c",
        "src/core/lib/transport/static_metadata.c",
        "src/core/lib/transport/status_conversion.c",
        "src/core/lib/transport/timeout_encoding.c",
        "src/core/lib/transport/transport.c",
        "src/core/lib/transport/transport_op_string.c",
    ],
    hdrs = [
        "src/core/lib/channel/channel_args.h",
        "src/core/lib/channel/channel_stack.h",
        "src/core/lib/channel/channel_stack_builder.h",
        "src/core/lib/channel/connected_channel.h",
        "src/core/lib/channel/context.h",
        "src/core/lib/channel/handshaker.h",
        "src/core/lib/channel/handshaker_factory.h",
        "src/core/lib/channel/handshaker_registry.h",
        "src/core/lib/compression/algorithm_metadata.h",
        "src/core/lib/compression/message_compress.h",
        "src/core/lib/compression/stream_compression.h",
        "src/core/lib/http/format_request.h",
        "src/core/lib/http/httpcli.h",
        "src/core/lib/http/parser.h",
        "src/core/lib/iomgr/closure.h",
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/endpoint.h",
        "src/core/lib/iomgr/endpoint_pair.h",
        "src/core/lib/iomgr/error.h",
        "src/core/lib/iomgr/error_internal.h",
        "src/core/lib/iomgr/ev_epoll1_linux.h",
        "src/core/lib/iomgr/ev_epoll_limited_pollers_linux.h",
        "src/core/lib/iomgr/ev_epoll_thread_pool_linux.h",
        "src/core/lib/iomgr/ev_epollex_linux.h",
        "src/core/lib/iomgr/ev_epollsig_linux.h",
        "src/core/lib/iomgr/ev_poll_posix.h",
        "src/core/lib/iomgr/ev_posix.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/gethostname.h",
        "src/core/lib/iomgr/iocp_windows.h",
        "src/core/lib/iomgr/iomgr.h",
        "src/core/lib/iomgr/iomgr_internal.h",
        "src/core/lib/iomgr/iomgr_posix.h",
        "src/core/lib/iomgr/iomgr_uv.h",
        "src/core/lib/iomgr/is_epollexclusive_available.h",
        "src/core/lib/iomgr/load_file.h",
        "src/core/lib/iomgr/lockfree_event.h",
        "src/core/lib/iomgr/nameser.h",
        "src/core/lib/iomgr/network_status_tracker.h",
        "src/core/lib/iomgr/polling_entity.h",
        "src/core/lib/iomgr/pollset.h",
        "src/core/lib/iomgr/pollset_set.h",
        "src/core/lib/iomgr/pollset_set_windows.h",
        "src/core/lib/iomgr/pollset_uv.h",
        "src/core/lib/iomgr/pollset_windows.h",
        "src/core/lib/iomgr/port.h",
        "src/core/lib/iomgr/resolve_address.h",
        "src/core/lib/iomgr/resource_quota.h",
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_posix.h",
        "src/core/lib/iomgr/sockaddr_utils.h",
        "src/core/lib/iomgr/sockaddr_windows.h",
        "src/core/lib/iomgr/socket_factory_posix.h",
        "src/core/lib/iomgr/socket_mutator.h",
        "src/core/lib/iomgr/socket_utils.h",
        "src/core/lib/iomgr/socket_utils_posix.h",
        "src/core/lib/iomgr/socket_windows.h",
        "src/core/lib/iomgr/sys_epoll_wrapper.h",
        "src/core/lib/iomgr/tcp_client.h",
        "src/core/lib/iomgr/tcp_client_posix.h",
        "src/core/lib/iomgr/tcp_posix.h",
        "src/core/lib/iomgr/tcp_server.h",
        "src/core/lib/iomgr/tcp_server_utils_posix.h",
        "src/core/lib/iomgr/tcp_uv.h",
        "src/core/lib/iomgr/tcp_windows.h",
        "src/core/lib/iomgr/time_averaged_stats.h",
        "src/core/lib/iomgr/timer.h",
        "src/core/lib/iomgr/timer_generic.h",
        "src/core/lib/iomgr/timer_heap.h",
        "src/core/lib/iomgr/timer_manager.h",
        "src/core/lib/iomgr/timer_uv.h",
        "src/core/lib/iomgr/udp_server.h",
        "src/core/lib/iomgr/unix_sockets_posix.h",
        "src/core/lib/iomgr/wakeup_fd_cv.h",
        "src/core/lib/iomgr/wakeup_fd_pipe.h",
        "src/core/lib/iomgr/wakeup_fd_posix.h",
        "src/core/lib/json/json.h",
        "src/core/lib/json/json_common.h",
        "src/core/lib/json/json_reader.h",
        "src/core/lib/json/json_writer.h",
        "src/core/lib/slice/b64.h",
        "src/core/lib/slice/percent_encoding.h",
        "src/core/lib/slice/slice_hash_table.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
        "src/core/lib/surface/alarm_internal.h",
        "src/core/lib/surface/api_trace.h",
        "src/core/lib/surface/call.h",
        "src/core/lib/surface/call_test_only.h",
        "src/core/lib/surface/channel.h",
        "src/core/lib/surface/channel_init.h",
        "src/core/lib/surface/channel_stack_type.h",
        "src/core/lib/surface/completion_queue.h",
        "src/core/lib/surface/completion_queue_factory.h",
        "src/core/lib/surface/event_string.h",
        "src/core/lib/surface/init.h",
        "src/core/lib/surface/lame_client.h",
        "src/core/lib/surface/server.h",
        "src/core/lib/surface/validate_metadata.h",
        "src/core/lib/transport/bdp_estimator.h",
        "src/core/lib/transport/byte_stream.h",
        "src/core/lib/transport/connectivity_state.h",
        "src/core/lib/transport/error_utils.h",
        "src/core/lib/transport/http2_errors.h",
        "src/core/lib/transport/metadata.h",
        "src/core/lib/transport/metadata_batch.h",
        "src/core/lib/transport/pid_controller.h",
        "src/core/lib/transport/service_config.h",
        "src/core/lib/transport/static_metadata.h",
        "src/core/lib/transport/status_conversion.h",
        "src/core/lib/transport/timeout_encoding.h",
        "src/core/lib/transport/transport.h",
        "src/core/lib/transport/transport_impl.h",
    ],
    external_deps = [
        "zlib",
    ],
    language = "c",
    public_hdrs = GRPC_PUBLIC_HDRS,
    deps = [
        "gpr_base",
        "grpc_codegen",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "grpc_base",
    srcs = [
        "src/core/lib/surface/lame_client.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base_c",
    ],
)

grpc_cc_library(
    name = "grpc_common",
    language = "c",
    deps = [
        "grpc_base",
        # standard plugins
        "census",
        "grpc_deadline_filter",
        "grpc_lb_policy_pick_first",
        "grpc_lb_policy_round_robin",
        "grpc_load_reporting",
        "grpc_max_age_filter",
        "grpc_message_size_filter",
        "grpc_resolver_dns_ares",
        "grpc_resolver_fake",
        "grpc_resolver_dns_native",
        "grpc_resolver_sockaddr",
        "grpc_transport_chttp2_client_insecure",
        "grpc_transport_chttp2_server_insecure",
        "grpc_transport_inproc",
        "grpc_workaround_cronet_compression_filter",
        "grpc_server_backward_compatibility",
    ],
)

grpc_cc_library(
    name = "grpc_client_channel",
    srcs = [
        "src/core/ext/filters/client_channel/channel_connectivity.c",
        "src/core/ext/filters/client_channel/client_channel.c",
        "src/core/ext/filters/client_channel/client_channel_factory.c",
        "src/core/ext/filters/client_channel/client_channel_plugin.c",
        "src/core/ext/filters/client_channel/connector.c",
        "src/core/ext/filters/client_channel/http_connect_handshaker.c",
        "src/core/ext/filters/client_channel/http_proxy.c",
        "src/core/ext/filters/client_channel/lb_policy.c",
        "src/core/ext/filters/client_channel/lb_policy_factory.c",
        "src/core/ext/filters/client_channel/lb_policy_registry.c",
        "src/core/ext/filters/client_channel/parse_address.c",
        "src/core/ext/filters/client_channel/proxy_mapper.c",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.c",
        "src/core/ext/filters/client_channel/resolver.c",
        "src/core/ext/filters/client_channel/resolver_factory.c",
        "src/core/ext/filters/client_channel/resolver_registry.c",
        "src/core/ext/filters/client_channel/retry_throttle.c",
        "src/core/ext/filters/client_channel/subchannel.c",
        "src/core/ext/filters/client_channel/subchannel_index.c",
        "src/core/ext/filters/client_channel/uri_parser.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/client_channel.h",
        "src/core/ext/filters/client_channel/client_channel_factory.h",
        "src/core/ext/filters/client_channel/connector.h",
        "src/core/ext/filters/client_channel/http_connect_handshaker.h",
        "src/core/ext/filters/client_channel/http_proxy.h",
        "src/core/ext/filters/client_channel/lb_policy.h",
        "src/core/ext/filters/client_channel/lb_policy_factory.h",
        "src/core/ext/filters/client_channel/lb_policy_registry.h",
        "src/core/ext/filters/client_channel/parse_address.h",
        "src/core/ext/filters/client_channel/proxy_mapper.h",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.h",
        "src/core/ext/filters/client_channel/resolver.h",
        "src/core/ext/filters/client_channel/resolver_factory.h",
        "src/core/ext/filters/client_channel/resolver_registry.h",
        "src/core/ext/filters/client_channel/retry_throttle.h",
        "src/core/ext/filters/client_channel/subchannel.h",
        "src/core/ext/filters/client_channel/subchannel_index.h",
        "src/core/ext/filters/client_channel/uri_parser.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_deadline_filter",
    ],
)

grpc_cc_library(
    name = "grpc_max_age_filter",
    srcs = [
        "src/core/ext/filters/max_age/max_age_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/max_age/max_age_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_deadline_filter",
    srcs = [
        "src/core/ext/filters/deadline/deadline_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/deadline/deadline_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_message_size_filter",
    srcs = [
        "src/core/ext/filters/message_size/message_size_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/message_size/message_size_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_http_filters",
    srcs = [
        "src/core/ext/filters/http/client/http_client_filter.c",
        "src/core/ext/filters/http/http_filters_plugin.c",
        "src/core/ext/filters/http/message_compress/message_compress_filter.c",
        "src/core/ext/filters/http/server/http_server_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/http/client/http_client_filter.h",
        "src/core/ext/filters/http/message_compress/message_compress_filter.h",
        "src/core/ext/filters/http/server/http_server_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_workaround_cronet_compression_filter",
    srcs = [
        "src/core/ext/filters/workarounds/workaround_cronet_compression_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/workarounds/workaround_cronet_compression_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_server_backward_compatibility",
    ],
)

grpc_cc_library(
    name = "grpc_codegen",
    language = "c",
    public_hdrs = [
        "include/grpc/impl/codegen/byte_buffer_reader.h",
        "include/grpc/impl/codegen/compression_types.h",
        "include/grpc/impl/codegen/connectivity_state.h",
        "include/grpc/impl/codegen/exec_ctx_fwd.h",
        "include/grpc/impl/codegen/grpc_types.h",
        "include/grpc/impl/codegen/propagation_bits.h",
        "include/grpc/impl/codegen/status.h",
        "include/grpc/impl/codegen/slice.h",
    ],
    deps = [
        "gpr_codegen",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_fake",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb_secure",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.c",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_fake",
        "grpc_secure",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_pick_first",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_load_reporting",
    srcs = [
        "src/core/ext/filters/load_reporting/load_reporting.c",
        "src/core/ext/filters/load_reporting/load_reporting_filter.c",
    ],
    hdrs = [
        "src/core/ext/filters/load_reporting/load_reporting.h",
        "src/core/ext/filters/load_reporting/load_reporting_filter.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_native",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.c",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.c",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.c",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "cares",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.c"],
    hdrs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"],
    language = "c",
    visibility = ["//test:__subpackages__"],
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_secure",
    srcs = [
        "src/core/lib/http/httpcli_security_connector.c",
        "src/core/lib/security/context/security_context.c",
        "src/core/lib/security/credentials/composite/composite_credentials.c",
        "src/core/lib/security/credentials/credentials.c",
        "src/core/lib/security/credentials/credentials_metadata.c",
        "src/core/lib/security/credentials/fake/fake_credentials.c",
        "src/core/lib/security/credentials/google_default/credentials_generic.c",
        "src/core/lib/security/credentials/google_default/google_default_credentials.c",
        "src/core/lib/security/credentials/iam/iam_credentials.c",
        "src/core/lib/security/credentials/jwt/json_token.c",
        "src/core/lib/security/credentials/jwt/jwt_credentials.c",
        "src/core/lib/security/credentials/jwt/jwt_verifier.c",
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.c",
        "src/core/lib/security/credentials/plugin/plugin_credentials.c",
        "src/core/lib/security/credentials/ssl/ssl_credentials.c",
        "src/core/lib/security/transport/client_auth_filter.c",
        "src/core/lib/security/transport/lb_targets_info.c",
        "src/core/lib/security/transport/secure_endpoint.c",
        "src/core/lib/security/transport/security_connector.c",
        "src/core/lib/security/transport/security_handshaker.c",
        "src/core/lib/security/transport/server_auth_filter.c",
        "src/core/lib/security/transport/tsi_error.c",
        "src/core/lib/security/util/json_util.c",
        "src/core/lib/surface/init_secure.c",
    ],
    hdrs = [
        "src/core/lib/security/context/security_context.h",
        "src/core/lib/security/credentials/composite/composite_credentials.h",
        "src/core/lib/security/credentials/credentials.h",
        "src/core/lib/security/credentials/fake/fake_credentials.h",
        "src/core/lib/security/credentials/google_default/google_default_credentials.h",
        "src/core/lib/security/credentials/iam/iam_credentials.h",
        "src/core/lib/security/credentials/jwt/json_token.h",
        "src/core/lib/security/credentials/jwt/jwt_credentials.h",
        "src/core/lib/security/credentials/jwt/jwt_verifier.h",
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.h",
        "src/core/lib/security/credentials/plugin/plugin_credentials.h",
        "src/core/lib/security/credentials/ssl/ssl_credentials.h",
        "src/core/lib/security/transport/auth_filters.h",
        "src/core/lib/security/transport/lb_targets_info.h",
        "src/core/lib/security/transport/secure_endpoint.h",
        "src/core/lib/security/transport/security_connector.h",
        "src/core/lib/security/transport/security_handshaker.h",
        "src/core/lib/security/transport/tsi_error.h",
        "src/core/lib/security/util/json_util.h",
    ],
    language = "c",
    public_hdrs = GRPC_SECURE_PUBLIC_HDRS,
    deps = [
        "grpc_base",
        "grpc_transport_chttp2_alpn",
        "tsi",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.c",
        "src/core/ext/transport/chttp2/transport/bin_encoder.c",
        "src/core/ext/transport/chttp2/transport/chttp2_plugin.c",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.c",
        "src/core/ext/transport/chttp2/transport/flow_control.c",
        "src/core/ext/transport/chttp2/transport/frame_data.c",
        "src/core/ext/transport/chttp2/transport/frame_goaway.c",
        "src/core/ext/transport/chttp2/transport/frame_ping.c",
        "src/core/ext/transport/chttp2/transport/frame_rst_stream.c",
        "src/core/ext/transport/chttp2/transport/frame_settings.c",
        "src/core/ext/transport/chttp2/transport/frame_window_update.c",
        "src/core/ext/transport/chttp2/transport/hpack_encoder.c",
        "src/core/ext/transport/chttp2/transport/hpack_parser.c",
        "src/core/ext/transport/chttp2/transport/hpack_table.c",
        "src/core/ext/transport/chttp2/transport/http2_settings.c",
        "src/core/ext/transport/chttp2/transport/huffsyms.c",
        "src/core/ext/transport/chttp2/transport/incoming_metadata.c",
        "src/core/ext/transport/chttp2/transport/parsing.c",
        "src/core/ext/transport/chttp2/transport/stream_lists.c",
        "src/core/ext/transport/chttp2/transport/stream_map.c",
        "src/core/ext/transport/chttp2/transport/varint.c",
        "src/core/ext/transport/chttp2/transport/writing.c",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.h",
        "src/core/ext/transport/chttp2/transport/bin_encoder.h",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.h",
        "src/core/ext/transport/chttp2/transport/frame.h",
        "src/core/ext/transport/chttp2/transport/frame_data.h",
        "src/core/ext/transport/chttp2/transport/frame_goaway.h",
        "src/core/ext/transport/chttp2/transport/frame_ping.h",
        "src/core/ext/transport/chttp2/transport/frame_rst_stream.h",
        "src/core/ext/transport/chttp2/transport/frame_settings.h",
        "src/core/ext/transport/chttp2/transport/frame_window_update.h",
        "src/core/ext/transport/chttp2/transport/hpack_encoder.h",
        "src/core/ext/transport/chttp2/transport/hpack_parser.h",
        "src/core/ext/transport/chttp2/transport/hpack_table.h",
        "src/core/ext/transport/chttp2/transport/http2_settings.h",
        "src/core/ext/transport/chttp2/transport/huffsyms.h",
        "src/core/ext/transport/chttp2/transport/incoming_metadata.h",
        "src/core/ext/transport/chttp2/transport/internal.h",
        "src/core/ext/transport/chttp2/transport/stream_map.h",
        "src/core/ext/transport/chttp2/transport/varint.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_http_filters",
        "grpc_transport_chttp2_alpn",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_alpn",
    srcs = [
        "src/core/ext/transport/chttp2/alpn/alpn.c",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/alpn/alpn.h",
    ],
    language = "c",
    deps = [
        "gpr",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_connector",
    srcs = [
        "src/core/ext/transport/chttp2/client/chttp2_connector.c",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/client/chttp2_connector.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_insecure",
    srcs = [
        "src/core/ext/transport/chttp2/client/insecure/channel_create.c",
        "src/core/ext/transport/chttp2/client/insecure/channel_create_posix.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_transport_chttp2",
        "grpc_transport_chttp2_client_connector",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_secure",
    srcs = [
        "src/core/ext/transport/chttp2/client/secure/secure_channel_create.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_secure",
        "grpc_transport_chttp2",
        "grpc_transport_chttp2_client_connector",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server",
    srcs = [
        "src/core/ext/transport/chttp2/server/chttp2_server.c",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/server/chttp2_server.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server_insecure",
    srcs = [
        "src/core/ext/transport/chttp2/server/insecure/server_chttp2.c",
        "src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_transport_chttp2",
        "grpc_transport_chttp2_server",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server_secure",
    srcs = [
        "src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.c",
    ],
    language = "c",
    deps = [
        "grpc_base",
        "grpc_secure",
        "grpc_transport_chttp2",
        "grpc_transport_chttp2_server",
    ],
)

grpc_cc_library(
    name = "grpc_transport_cronet_client_secure",
    srcs = [
        "src/core/ext/transport/cronet/client/secure/cronet_channel_create.c",
        "src/core/ext/transport/cronet/transport/cronet_api_dummy.c",
        "src/core/ext/transport/cronet/transport/cronet_transport.c",
    ],
    hdrs = [
        "src/core/ext/transport/cronet/transport/cronet_transport.h",
        "third_party/objective_c/Cronet/bidirectional_stream_c.h",
    ],
    language = "c",
    public_hdrs = [
        "include/grpc/grpc_cronet.h",
        "include/grpc/grpc_security.h",
        "include/grpc/grpc_security_constants.h",
    ],
    deps = [
        "grpc_base",
        "grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_transport_inproc",
    srcs = [
        "src/core/ext/transport/inproc/inproc_plugin.c",
        "src/core/ext/transport/inproc/inproc_transport.c",
    ],
    hdrs = [
        "src/core/ext/transport/inproc/inproc_transport.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "tsi",
    srcs = [
        "src/core/tsi/fake_transport_security.c",
        "src/core/tsi/gts_transport_security.c",
        "src/core/tsi/ssl_transport_security.c",
        "src/core/tsi/transport_security.c",
        "src/core/tsi/transport_security_adapter.c",
        "src/core/tsi/transport_security_grpc.c",
    ],
    hdrs = [
        "src/core/tsi/fake_transport_security.h",
        "src/core/tsi/gts_transport_security.h",
        "src/core/tsi/ssl_transport_security.h",
        "src/core/tsi/ssl_types.h",
        "src/core/tsi/transport_security.h",
        "src/core/tsi/transport_security_adapter.h",
        "src/core/tsi/transport_security_grpc.h",
        "src/core/tsi/transport_security_interface.h",
    ],
    external_deps = [
        "libssl",
    ],
    language = "c",
    deps = [
        "gpr",
        "grpc_base",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "grpc++_base",
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "grpc",
        "grpc++_codegen_base",
    ],
)

grpc_cc_library(
    name = "grpc++_base_unsecure",
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "grpc++_codegen_base",
        "grpc_unsecure",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_base",
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/async_stream.h",
        "include/grpc++/impl/codegen/async_unary_call.h",
        "include/grpc++/impl/codegen/call.h",
        "include/grpc++/impl/codegen/call_hook.h",
        "include/grpc++/impl/codegen/channel_interface.h",
        "include/grpc++/impl/codegen/client_context.h",
        "include/grpc++/impl/codegen/client_unary_call.h",
        "include/grpc++/impl/codegen/completion_queue.h",
        "include/grpc++/impl/codegen/completion_queue_tag.h",
        "include/grpc++/impl/codegen/config.h",
        "include/grpc++/impl/codegen/core_codegen_interface.h",
        "include/grpc++/impl/codegen/create_auth_context.h",
        "include/grpc++/impl/codegen/grpc_library.h",
        "include/grpc++/impl/codegen/metadata_map.h",
        "include/grpc++/impl/codegen/method_handler_impl.h",
        "include/grpc++/impl/codegen/rpc_method.h",
        "include/grpc++/impl/codegen/rpc_service_method.h",
        "include/grpc++/impl/codegen/security/auth_context.h",
        "include/grpc++/impl/codegen/serialization_traits.h",
        "include/grpc++/impl/codegen/server_context.h",
        "include/grpc++/impl/codegen/server_interface.h",
        "include/grpc++/impl/codegen/service_type.h",
        "include/grpc++/impl/codegen/slice.h",
        "include/grpc++/impl/codegen/status.h",
        "include/grpc++/impl/codegen/status_code_enum.h",
        "include/grpc++/impl/codegen/string_ref.h",
        "include/grpc++/impl/codegen/stub_options.h",
        "include/grpc++/impl/codegen/sync_stream.h",
        "include/grpc++/impl/codegen/time.h",
    ],
    deps = [
        "grpc_codegen",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_base_src",
    srcs = [
        "src/cpp/codegen/codegen_init.cc",
    ],
    language = "c++",
    deps = [
        "grpc++_codegen_base",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_proto",
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/proto_utils.h",
    ],
    deps = [
        "grpc++_codegen_base",
        "grpc++_config_proto",
    ],
)

grpc_cc_library(
    name = "grpc++_config_proto",
    external_deps = [
        "protobuf",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/config_protobuf.h",
    ],
)

grpc_cc_library(
    name = "grpc++_reflection",
    srcs = [
        "src/cpp/ext/proto_server_reflection.cc",
        "src/cpp/ext/proto_server_reflection_plugin.cc",
    ],
    hdrs = [
        "src/cpp/ext/proto_server_reflection.h",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/ext/proto_server_reflection_plugin.h",
    ],
    deps = [
        ":grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpc++_test",
    public_hdrs = [
        "include/grpc++/test/mock_stream.h",
        "include/grpc++/test/server_context_test_spouse.h",
    ],
    deps = [
        ":grpc++",
    ],
)

grpc_cc_library(
    name = "grpc_server_backward_compatibility",
    srcs = [
        "src/core/ext/filters/workarounds/workaround_utils.c",
    ],
    hdrs = [
        "src/core/ext/filters/workarounds/workaround_utils.h",
    ],
    language = "c",
    deps = [
        "grpc_base",
    ],
)

grpc_generate_one_off_targets()
