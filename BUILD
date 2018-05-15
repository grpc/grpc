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

config_setting(
    name = "grpc_no_ares",
    values = {"define": "grpc_no_ares=true"},
)

config_setting(
    name = "grpc_allow_exceptions",
    values = {"define": "GRPC_ALLOW_EXCEPTIONS=1"},
)

config_setting(
    name = "grpc_disallow_exceptions",
    values = {"define": "GRPC_ALLOW_EXCEPTIONS=0"},
)

config_setting(
    name = "remote_execution",
    values = {"define": "GRPC_PORT_ISOLATED_RUNTIME=1"},
)

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows"},
)

config_setting(
    name = "windows_msvc",
    values = {"cpu": "x64_windows_msvc"},
)

# This should be updated along with build.yaml
g_stands_for = "glorious"

core_version = "6.0.0"

version = "1.12.0"

GPR_PUBLIC_HDRS = [
    "include/grpc/support/alloc.h",
    "include/grpc/support/atm.h",
    "include/grpc/support/atm_gcc_atomic.h",
    "include/grpc/support/atm_gcc_sync.h",
    "include/grpc/support/atm_windows.h",
    "include/grpc/support/cpu.h",
    "include/grpc/support/log.h",
    "include/grpc/support/log_windows.h",
    "include/grpc/support/port_platform.h",
    "include/grpc/support/string_util.h",
    "include/grpc/support/sync.h",
    "include/grpc/support/sync_custom.h",
    "include/grpc/support/sync_generic.h",
    "include/grpc/support/sync_posix.h",
    "include/grpc/support/sync_windows.h",
    "include/grpc/support/thd_id.h",
    "include/grpc/support/time.h",
]

GRPC_PUBLIC_HDRS = [
    "include/grpc/byte_buffer.h",
    "include/grpc/byte_buffer_reader.h",
    "include/grpc/compression.h",
    "include/grpc/fork.h",
    "include/grpc/grpc.h",
    "include/grpc/grpc_posix.h",
    "include/grpc/grpc_security_constants.h",
    "include/grpc/load_reporting.h",
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
    "src/cpp/common/alarm.cc",
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
    "include/grpcpp/alarm.h",
    "include/grpcpp/channel.h",
    "include/grpcpp/client_context.h",
    "include/grpcpp/completion_queue.h",
    "include/grpcpp/create_channel.h",
    "include/grpcpp/create_channel_posix.h",
    "include/grpcpp/ext/health_check_service_server_builder_option.h",
    "include/grpcpp/generic/async_generic_service.h",
    "include/grpcpp/generic/generic_stub.h",
    "include/grpcpp/grpcpp.h",
    "include/grpcpp/health_check_service_interface.h",
    "include/grpcpp/impl/call.h",
    "include/grpcpp/impl/channel_argument_option.h",
    "include/grpcpp/impl/client_unary_call.h",
    "include/grpcpp/impl/codegen/core_codegen.h",
    "include/grpcpp/impl/grpc_library.h",
    "include/grpcpp/impl/method_handler_impl.h",
    "include/grpcpp/impl/rpc_method.h",
    "include/grpcpp/impl/rpc_service_method.h",
    "include/grpcpp/impl/serialization_traits.h",
    "include/grpcpp/impl/server_builder_option.h",
    "include/grpcpp/impl/server_builder_plugin.h",
    "include/grpcpp/impl/server_initializer.h",
    "include/grpcpp/impl/service_type.h",
    "include/grpcpp/impl/sync_cxx11.h",
    "include/grpcpp/impl/sync_no_cxx11.h",
    "include/grpcpp/resource_quota.h",
    "include/grpcpp/security/auth_context.h",
    "include/grpcpp/security/auth_metadata_processor.h",
    "include/grpcpp/security/credentials.h",
    "include/grpcpp/security/server_credentials.h",
    "include/grpcpp/server.h",
    "include/grpcpp/server_builder.h",
    "include/grpcpp/server_context.h",
    "include/grpcpp/server_posix.h",
    "include/grpcpp/support/async_stream.h",
    "include/grpcpp/support/async_unary_call.h",
    "include/grpcpp/support/byte_buffer.h",
    "include/grpcpp/support/channel_arguments.h",
    "include/grpcpp/support/config.h",
    "include/grpcpp/support/proto_buffer_reader.h",
    "include/grpcpp/support/proto_buffer_writer.h",
    "include/grpcpp/support/slice.h",
    "include/grpcpp/support/status.h",
    "include/grpcpp/support/status_code_enum.h",
    "include/grpcpp/support/string_ref.h",
    "include/grpcpp/support/stub_options.h",
    "include/grpcpp/support/sync_stream.h",
    "include/grpcpp/support/time.h",
]

grpc_cc_library(
    name = "gpr",
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    standalone = True,
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "grpc_unsecure",
    srcs = [
        "src/core/lib/surface/init.cc",
        "src/core/lib/surface/init_unsecure.cc",
        "src/core/plugin_registry/grpc_unsecure_plugin_registry.cc",
    ],
    language = "c++",
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
        "src/core/lib/surface/init.cc",
        "src/core/plugin_registry/grpc_plugin_registry.cc",
    ],
    language = "c++",
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
        "src/core/lib/surface/init.cc",
        "src/core/plugin_registry/grpc_cronet_plugin_registry.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_http_filters",
        "grpc_transport_chttp2_client_secure",
        "grpc_transport_cronet_client_secure",
    ],
)

grpc_cc_library(
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
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
        "include/grpcpp/support/error_details.h",
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
        "src/core/ext/census/grpc_context.cc",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c++",
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
        "src/core/lib/gpr/alloc.cc",
        "src/core/lib/gpr/arena.cc",
        "src/core/lib/gpr/atm.cc",
        "src/core/lib/gpr/cpu_iphone.cc",
        "src/core/lib/gpr/cpu_linux.cc",
        "src/core/lib/gpr/cpu_posix.cc",
        "src/core/lib/gpr/cpu_windows.cc",
        "src/core/lib/gpr/env_linux.cc",
        "src/core/lib/gpr/env_posix.cc",
        "src/core/lib/gpr/env_windows.cc",
        "src/core/lib/gpr/fork.cc",
        "src/core/lib/gpr/host_port.cc",
        "src/core/lib/gpr/log.cc",
        "src/core/lib/gpr/log_android.cc",
        "src/core/lib/gpr/log_linux.cc",
        "src/core/lib/gpr/log_posix.cc",
        "src/core/lib/gpr/log_windows.cc",
        "src/core/lib/gpr/mpscq.cc",
        "src/core/lib/gpr/murmur_hash.cc",
        "src/core/lib/gpr/string.cc",
        "src/core/lib/gpr/string_posix.cc",
        "src/core/lib/gpr/string_util_windows.cc",
        "src/core/lib/gpr/string_windows.cc",
        "src/core/lib/gpr/sync.cc",
        "src/core/lib/gpr/sync_posix.cc",
        "src/core/lib/gpr/sync_windows.cc",
        "src/core/lib/gpr/time.cc",
        "src/core/lib/gpr/time_posix.cc",
        "src/core/lib/gpr/time_precise.cc",
        "src/core/lib/gpr/time_windows.cc",
        "src/core/lib/gpr/tls_pthread.cc",
        "src/core/lib/gpr/tmpfile_msys.cc",
        "src/core/lib/gpr/tmpfile_posix.cc",
        "src/core/lib/gpr/tmpfile_windows.cc",
        "src/core/lib/gpr/wrap_memcpy.cc",
        "src/core/lib/gprpp/thd_posix.cc",
        "src/core/lib/gprpp/thd_windows.cc",
        "src/core/lib/profiling/basic_timers.cc",
        "src/core/lib/profiling/stap_timers.cc",
    ],
    hdrs = [
        "src/core/lib/gpr/arena.h",
        "src/core/lib/gpr/env.h",
        "src/core/lib/gpr/fork.h",
        "src/core/lib/gpr/host_port.h",
        "src/core/lib/gpr/mpscq.h",
        "src/core/lib/gpr/murmur_hash.h",
        "src/core/lib/gpr/spinlock.h",
        "src/core/lib/gpr/string.h",
        "src/core/lib/gpr/string_windows.h",
        "src/core/lib/gpr/time_precise.h",
        "src/core/lib/gpr/tls.h",
        "src/core/lib/gpr/tls_gcc.h",
        "src/core/lib/gpr/tls_msvc.h",
        "src/core/lib/gpr/tls_pthread.h",
        "src/core/lib/gpr/tmpfile.h",
        "src/core/lib/gpr/useful.h",
        "src/core/lib/gprpp/abstract.h",
        "src/core/lib/gprpp/manual_constructor.h",
        "src/core/lib/gprpp/memory.h",
        "src/core/lib/gprpp/thd.h",
        "src/core/lib/profiling/timers.h",
    ],
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    deps = [
        "gpr_codegen",
    ],
)

grpc_cc_library(
    name = "gpr_codegen",
    language = "c++",
    public_hdrs = [
        "include/grpc/impl/codegen/atm.h",
        "include/grpc/impl/codegen/atm_gcc_atomic.h",
        "include/grpc/impl/codegen/atm_gcc_sync.h",
        "include/grpc/impl/codegen/atm_windows.h",
        "include/grpc/impl/codegen/fork.h",
        "include/grpc/impl/codegen/gpr_slice.h",
        "include/grpc/impl/codegen/gpr_types.h",
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/impl/codegen/sync.h",
        "include/grpc/impl/codegen/sync_custom.h",
        "include/grpc/impl/codegen/sync_generic.h",
        "include/grpc/impl/codegen/sync_posix.h",
        "include/grpc/impl/codegen/sync_windows.h",
    ],
)

grpc_cc_library(
    name = "grpc_trace",
    srcs = ["src/core/lib/debug/trace.cc"],
    hdrs = ["src/core/lib/debug/trace.h"],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    deps = [
        "grpc_codegen",
        ":gpr",
    ],
)

grpc_cc_library(
    name = "atomic",
    hdrs = [
        "src/core/lib/gprpp/atomic_with_atm.h",
        "src/core/lib/gprpp/atomic_with_std.h",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/gprpp/atomic.h",
    ],
    deps = [
        "gpr",
    ],
)

grpc_cc_library(
    name = "inlined_vector",
    language = "c++",
    public_hdrs = [
        "src/core/lib/gprpp/inlined_vector.h",
    ],
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "debug_location",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/debug_location.h"],
)

grpc_cc_library(
    name = "orphanable",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/orphanable.h"],
    deps = [
        "debug_location",
        "gpr_base",
        "grpc_trace",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "ref_counted",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/ref_counted.h"],
    deps = [
        "debug_location",
        "gpr_base",
        "grpc_trace",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "ref_counted_ptr",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/ref_counted_ptr.h"],
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "grpc_base_c",
    srcs = [
        "src/core/lib/avl/avl.cc",
        "src/core/lib/backoff/backoff.cc",
        "src/core/lib/channel/channel_args.cc",
        "src/core/lib/channel/channel_stack.cc",
        "src/core/lib/channel/channel_stack_builder.cc",
        "src/core/lib/channel/channel_trace.cc",
        "src/core/lib/channel/channel_trace_registry.cc",
        "src/core/lib/channel/connected_channel.cc",
        "src/core/lib/channel/handshaker.cc",
        "src/core/lib/channel/handshaker_factory.cc",
        "src/core/lib/channel/handshaker_registry.cc",
        "src/core/lib/channel/status_util.cc",
        "src/core/lib/compression/compression.cc",
        "src/core/lib/compression/compression_internal.cc",
        "src/core/lib/compression/message_compress.cc",
        "src/core/lib/compression/stream_compression.cc",
        "src/core/lib/compression/stream_compression_gzip.cc",
        "src/core/lib/compression/stream_compression_identity.cc",
        "src/core/lib/debug/stats.cc",
        "src/core/lib/debug/stats_data.cc",
        "src/core/lib/http/format_request.cc",
        "src/core/lib/http/httpcli.cc",
        "src/core/lib/http/parser.cc",
        "src/core/lib/iomgr/call_combiner.cc",
        "src/core/lib/iomgr/combiner.cc",
        "src/core/lib/iomgr/endpoint.cc",
        "src/core/lib/iomgr/endpoint_pair_posix.cc",
        "src/core/lib/iomgr/endpoint_pair_uv.cc",
        "src/core/lib/iomgr/endpoint_pair_windows.cc",
        "src/core/lib/iomgr/error.cc",
        "src/core/lib/iomgr/ev_epoll1_linux.cc",
        "src/core/lib/iomgr/ev_epollex_linux.cc",
        "src/core/lib/iomgr/ev_epollsig_linux.cc",
        "src/core/lib/iomgr/ev_poll_posix.cc",
        "src/core/lib/iomgr/ev_posix.cc",
        "src/core/lib/iomgr/ev_windows.cc",
        "src/core/lib/iomgr/exec_ctx.cc",
        "src/core/lib/iomgr/executor.cc",
        "src/core/lib/iomgr/fork_posix.cc",
        "src/core/lib/iomgr/fork_windows.cc",
        "src/core/lib/iomgr/gethostname_fallback.cc",
        "src/core/lib/iomgr/gethostname_host_name_max.cc",
        "src/core/lib/iomgr/gethostname_sysconf.cc",
        "src/core/lib/iomgr/iocp_windows.cc",
        "src/core/lib/iomgr/iomgr.cc",
        "src/core/lib/iomgr/iomgr_custom.cc",
        "src/core/lib/iomgr/iomgr_internal.cc",
        "src/core/lib/iomgr/iomgr_posix.cc",
        "src/core/lib/iomgr/iomgr_windows.cc",
        "src/core/lib/iomgr/is_epollexclusive_available.cc",
        "src/core/lib/iomgr/load_file.cc",
        "src/core/lib/iomgr/lockfree_event.cc",
        "src/core/lib/iomgr/network_status_tracker.cc",
        "src/core/lib/iomgr/polling_entity.cc",
        "src/core/lib/iomgr/pollset.cc",
        "src/core/lib/iomgr/pollset_custom.cc",
        "src/core/lib/iomgr/pollset_set.cc",
        "src/core/lib/iomgr/pollset_set_custom.cc",
        "src/core/lib/iomgr/pollset_set_windows.cc",
        "src/core/lib/iomgr/pollset_uv.cc",
        "src/core/lib/iomgr/pollset_windows.cc",
        "src/core/lib/iomgr/resolve_address.cc",
        "src/core/lib/iomgr/resolve_address_custom.cc",
        "src/core/lib/iomgr/resolve_address_posix.cc",
        "src/core/lib/iomgr/resolve_address_windows.cc",
        "src/core/lib/iomgr/resource_quota.cc",
        "src/core/lib/iomgr/sockaddr_utils.cc",
        "src/core/lib/iomgr/socket_factory_posix.cc",
        "src/core/lib/iomgr/socket_mutator.cc",
        "src/core/lib/iomgr/socket_utils_common_posix.cc",
        "src/core/lib/iomgr/socket_utils_linux.cc",
        "src/core/lib/iomgr/socket_utils_posix.cc",
        "src/core/lib/iomgr/socket_utils_windows.cc",
        "src/core/lib/iomgr/socket_windows.cc",
        "src/core/lib/iomgr/tcp_client.cc",
        "src/core/lib/iomgr/tcp_client_custom.cc",
        "src/core/lib/iomgr/tcp_client_posix.cc",
        "src/core/lib/iomgr/tcp_client_windows.cc",
        "src/core/lib/iomgr/tcp_custom.cc",
        "src/core/lib/iomgr/tcp_posix.cc",
        "src/core/lib/iomgr/tcp_server.cc",
        "src/core/lib/iomgr/tcp_server_custom.cc",
        "src/core/lib/iomgr/tcp_server_posix.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_common.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc",
        "src/core/lib/iomgr/tcp_server_windows.cc",
        "src/core/lib/iomgr/tcp_uv.cc",
        "src/core/lib/iomgr/tcp_windows.cc",
        "src/core/lib/iomgr/time_averaged_stats.cc",
        "src/core/lib/iomgr/timer.cc",
        "src/core/lib/iomgr/timer_custom.cc",
        "src/core/lib/iomgr/timer_generic.cc",
        "src/core/lib/iomgr/timer_heap.cc",
        "src/core/lib/iomgr/timer_manager.cc",
        "src/core/lib/iomgr/timer_uv.cc",
        "src/core/lib/iomgr/udp_server.cc",
        "src/core/lib/iomgr/unix_sockets_posix.cc",
        "src/core/lib/iomgr/unix_sockets_posix_noop.cc",
        "src/core/lib/iomgr/wakeup_fd_cv.cc",
        "src/core/lib/iomgr/wakeup_fd_eventfd.cc",
        "src/core/lib/iomgr/wakeup_fd_nospecial.cc",
        "src/core/lib/iomgr/wakeup_fd_pipe.cc",
        "src/core/lib/iomgr/wakeup_fd_posix.cc",
        "src/core/lib/json/json.cc",
        "src/core/lib/json/json_reader.cc",
        "src/core/lib/json/json_string.cc",
        "src/core/lib/json/json_writer.cc",
        "src/core/lib/slice/b64.cc",
        "src/core/lib/slice/percent_encoding.cc",
        "src/core/lib/slice/slice.cc",
        "src/core/lib/slice/slice_buffer.cc",
        "src/core/lib/slice/slice_intern.cc",
        "src/core/lib/slice/slice_string_helpers.cc",
        "src/core/lib/surface/api_trace.cc",
        "src/core/lib/surface/byte_buffer.cc",
        "src/core/lib/surface/byte_buffer_reader.cc",
        "src/core/lib/surface/call.cc",
        "src/core/lib/surface/call_details.cc",
        "src/core/lib/surface/call_log_batch.cc",
        "src/core/lib/surface/channel.cc",
        "src/core/lib/surface/channel_init.cc",
        "src/core/lib/surface/channel_ping.cc",
        "src/core/lib/surface/channel_stack_type.cc",
        "src/core/lib/surface/completion_queue.cc",
        "src/core/lib/surface/completion_queue_factory.cc",
        "src/core/lib/surface/event_string.cc",
        "src/core/lib/surface/metadata_array.cc",
        "src/core/lib/surface/server.cc",
        "src/core/lib/surface/validate_metadata.cc",
        "src/core/lib/surface/version.cc",
        "src/core/lib/transport/bdp_estimator.cc",
        "src/core/lib/transport/byte_stream.cc",
        "src/core/lib/transport/connectivity_state.cc",
        "src/core/lib/transport/error_utils.cc",
        "src/core/lib/transport/metadata.cc",
        "src/core/lib/transport/metadata_batch.cc",
        "src/core/lib/transport/pid_controller.cc",
        "src/core/lib/transport/service_config.cc",
        "src/core/lib/transport/static_metadata.cc",
        "src/core/lib/transport/status_conversion.cc",
        "src/core/lib/transport/status_metadata.cc",
        "src/core/lib/transport/timeout_encoding.cc",
        "src/core/lib/transport/transport.cc",
        "src/core/lib/transport/transport_op_string.cc",
    ],
    hdrs = [
        "src/core/lib/avl/avl.h",
        "src/core/lib/backoff/backoff.h",
        "src/core/lib/channel/channel_args.h",
        "src/core/lib/channel/channel_stack.h",
        "src/core/lib/channel/channel_stack_builder.h",
        "src/core/lib/channel/channel_trace.h",
        "src/core/lib/channel/channel_trace_registry.h",
        "src/core/lib/channel/connected_channel.h",
        "src/core/lib/channel/context.h",
        "src/core/lib/channel/handshaker.h",
        "src/core/lib/channel/handshaker_factory.h",
        "src/core/lib/channel/handshaker_registry.h",
        "src/core/lib/channel/status_util.h",
        "src/core/lib/compression/algorithm_metadata.h",
        "src/core/lib/compression/compression_internal.h",
        "src/core/lib/compression/message_compress.h",
        "src/core/lib/compression/stream_compression.h",
        "src/core/lib/compression/stream_compression_gzip.h",
        "src/core/lib/compression/stream_compression_identity.h",
        "src/core/lib/debug/stats.h",
        "src/core/lib/debug/stats_data.h",
        "src/core/lib/http/format_request.h",
        "src/core/lib/http/httpcli.h",
        "src/core/lib/http/parser.h",
        "src/core/lib/iomgr/block_annotate.h",
        "src/core/lib/iomgr/call_combiner.h",
        "src/core/lib/iomgr/closure.h",
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/endpoint.h",
        "src/core/lib/iomgr/endpoint_pair.h",
        "src/core/lib/iomgr/error.h",
        "src/core/lib/iomgr/error_internal.h",
        "src/core/lib/iomgr/ev_epoll1_linux.h",
        "src/core/lib/iomgr/ev_epollex_linux.h",
        "src/core/lib/iomgr/ev_epollsig_linux.h",
        "src/core/lib/iomgr/ev_poll_posix.h",
        "src/core/lib/iomgr/ev_posix.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/gethostname.h",
        "src/core/lib/iomgr/iocp_windows.h",
        "src/core/lib/iomgr/iomgr.h",
        "src/core/lib/iomgr/iomgr_custom.h",
        "src/core/lib/iomgr/iomgr_internal.h",
        "src/core/lib/iomgr/iomgr_posix.h",
        "src/core/lib/iomgr/is_epollexclusive_available.h",
        "src/core/lib/iomgr/load_file.h",
        "src/core/lib/iomgr/lockfree_event.h",
        "src/core/lib/iomgr/nameser.h",
        "src/core/lib/iomgr/network_status_tracker.h",
        "src/core/lib/iomgr/polling_entity.h",
        "src/core/lib/iomgr/pollset.h",
        "src/core/lib/iomgr/pollset_custom.h",
        "src/core/lib/iomgr/pollset_set.h",
        "src/core/lib/iomgr/pollset_set_custom.h",
        "src/core/lib/iomgr/pollset_set_windows.h",
        "src/core/lib/iomgr/pollset_uv.h",
        "src/core/lib/iomgr/pollset_windows.h",
        "src/core/lib/iomgr/port.h",
        "src/core/lib/iomgr/resolve_address.h",
        "src/core/lib/iomgr/resolve_address_custom.h",
        "src/core/lib/iomgr/resource_quota.h",
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_custom.h",
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
        "src/core/lib/iomgr/tcp_custom.h",
        "src/core/lib/iomgr/tcp_posix.h",
        "src/core/lib/iomgr/tcp_server.h",
        "src/core/lib/iomgr/tcp_server_utils_posix.h",
        "src/core/lib/iomgr/tcp_windows.h",
        "src/core/lib/iomgr/time_averaged_stats.h",
        "src/core/lib/iomgr/timer.h",
        "src/core/lib/iomgr/timer_custom.h",
        "src/core/lib/iomgr/timer_generic.h",
        "src/core/lib/iomgr/timer_heap.h",
        "src/core/lib/iomgr/timer_manager.h",
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
        "src/core/lib/slice/slice_weak_hash_table.h",
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
        "src/core/lib/transport/status_metadata.h",
        "src/core/lib/transport/timeout_encoding.h",
        "src/core/lib/transport/transport.h",
        "src/core/lib/transport/transport_impl.h",
    ],
    external_deps = [
        "zlib",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    deps = [
        "gpr_base",
        "grpc_codegen",
        "grpc_trace",
        "inlined_vector",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_base",
    srcs = [
        "src/core/lib/surface/lame_client.cc",
    ],
    language = "c++",
    deps = [
        "atomic",
        "grpc_base_c",
    ],
)

grpc_cc_library(
    name = "grpc_common",
    language = "c++",
    deps = [
        "grpc_base",
        # standard plugins
        "census",
        "grpc_deadline_filter",
        "grpc_client_authority_filter",
        "grpc_lb_policy_pick_first",
        "grpc_lb_policy_round_robin",
        "grpc_server_load_reporting",
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
        "src/core/ext/filters/client_channel/backup_poller.cc",
        "src/core/ext/filters/client_channel/channel_connectivity.cc",
        "src/core/ext/filters/client_channel/client_channel.cc",
        "src/core/ext/filters/client_channel/client_channel_factory.cc",
        "src/core/ext/filters/client_channel/client_channel_plugin.cc",
        "src/core/ext/filters/client_channel/connector.cc",
        "src/core/ext/filters/client_channel/http_connect_handshaker.cc",
        "src/core/ext/filters/client_channel/http_proxy.cc",
        "src/core/ext/filters/client_channel/lb_policy.cc",
        "src/core/ext/filters/client_channel/lb_policy_factory.cc",
        "src/core/ext/filters/client_channel/lb_policy_registry.cc",
        "src/core/ext/filters/client_channel/method_params.cc",
        "src/core/ext/filters/client_channel/parse_address.cc",
        "src/core/ext/filters/client_channel/proxy_mapper.cc",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.cc",
        "src/core/ext/filters/client_channel/resolver.cc",
        "src/core/ext/filters/client_channel/resolver_registry.cc",
        "src/core/ext/filters/client_channel/retry_throttle.cc",
        "src/core/ext/filters/client_channel/subchannel.cc",
        "src/core/ext/filters/client_channel/subchannel_index.cc",
        "src/core/ext/filters/client_channel/uri_parser.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/backup_poller.h",
        "src/core/ext/filters/client_channel/client_channel.h",
        "src/core/ext/filters/client_channel/client_channel_factory.h",
        "src/core/ext/filters/client_channel/connector.h",
        "src/core/ext/filters/client_channel/http_connect_handshaker.h",
        "src/core/ext/filters/client_channel/http_proxy.h",
        "src/core/ext/filters/client_channel/lb_policy.h",
        "src/core/ext/filters/client_channel/lb_policy_factory.h",
        "src/core/ext/filters/client_channel/lb_policy_registry.h",
        "src/core/ext/filters/client_channel/method_params.h",
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
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_authority_filter",
        "grpc_deadline_filter",
        "inlined_vector",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_max_age_filter",
    srcs = [
        "src/core/ext/filters/max_age/max_age_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/max_age/max_age_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_deadline_filter",
    srcs = [
        "src/core/ext/filters/deadline/deadline_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/deadline/deadline_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_client_authority_filter",
    srcs = [
        "src/core/ext/filters/http/client_authority_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/http/client_authority_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_message_size_filter",
    srcs = [
        "src/core/ext/filters/message_size/message_size_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/message_size/message_size_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_http_filters",
    srcs = [
        "src/core/ext/filters/http/client/http_client_filter.cc",
        "src/core/ext/filters/http/http_filters_plugin.cc",
        "src/core/ext/filters/http/message_compress/message_compress_filter.cc",
        "src/core/ext/filters/http/server/http_server_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/http/client/http_client_filter.h",
        "src/core/ext/filters/http/message_compress/message_compress_filter.h",
        "src/core/ext/filters/http/server/http_server_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_workaround_cronet_compression_filter",
    srcs = [
        "src/core/ext/filters/workarounds/workaround_cronet_compression_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/workarounds/workaround_cronet_compression_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_server_backward_compatibility",
    ],
)

grpc_cc_library(
    name = "grpc_codegen",
    language = "c++",
    public_hdrs = [
        "include/grpc/impl/codegen/byte_buffer.h",
        "include/grpc/impl/codegen/byte_buffer_reader.h",
        "include/grpc/impl/codegen/compression_types.h",
        "include/grpc/impl/codegen/connectivity_state.h",
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
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_fake",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb_secure",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel_secure.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.c",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/proto/grpc/lb/v1/load_balancer.pb.h",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_fake",
        "grpc_secure",
    ],
)

grpc_cc_library(
    name = "grpc_lb_subchannel_list",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_pick_first",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_subchannel_list",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_subchannel_list",
    ],
)

grpc_cc_library(
    name = "grpc_server_load_reporting",
    srcs = [
        "src/core/ext/filters/load_reporting/server_load_reporting_filter.cc",
        "src/core/ext/filters/load_reporting/server_load_reporting_plugin.cc",
    ],
    hdrs = [
        "src/core/ext/filters/load_reporting/server_load_reporting_filter.h",
        "src/core/ext/filters/load_reporting/server_load_reporting_plugin.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "lb_load_data_store",
    srcs = [
        "src/cpp/server/load_reporter/load_data_store.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/load_data_store.h",
    ],
    language = "c++",
    deps = [
        "grpc++",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_native",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_fallback.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "cares",
        "address_sorting",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc"],
    hdrs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"],
    language = "c++",
    visibility = ["//test:__subpackages__"],
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_secure",
    srcs = [
        "src/core/lib/http/httpcli_security_connector.cc",
        "src/core/lib/security/context/security_context.cc",
        "src/core/lib/security/credentials/alts/alts_credentials.cc",
        "src/core/lib/security/credentials/composite/composite_credentials.cc",
        "src/core/lib/security/credentials/credentials.cc",
        "src/core/lib/security/credentials/credentials_metadata.cc",
        "src/core/lib/security/credentials/fake/fake_credentials.cc",
        "src/core/lib/security/credentials/google_default/credentials_generic.cc",
        "src/core/lib/security/credentials/google_default/google_default_credentials.cc",
        "src/core/lib/security/credentials/iam/iam_credentials.cc",
        "src/core/lib/security/credentials/jwt/json_token.cc",
        "src/core/lib/security/credentials/jwt/jwt_credentials.cc",
        "src/core/lib/security/credentials/jwt/jwt_verifier.cc",
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.cc",
        "src/core/lib/security/credentials/plugin/plugin_credentials.cc",
        "src/core/lib/security/credentials/ssl/ssl_credentials.cc",
        "src/core/lib/security/security_connector/alts_security_connector.cc",
        "src/core/lib/security/security_connector/security_connector.cc",
        "src/core/lib/security/transport/client_auth_filter.cc",
        "src/core/lib/security/transport/secure_endpoint.cc",
        "src/core/lib/security/transport/security_handshaker.cc",
        "src/core/lib/security/transport/server_auth_filter.cc",
        "src/core/lib/security/transport/target_authority_table.cc",
        "src/core/lib/security/transport/tsi_error.cc",
        "src/core/lib/security/util/json_util.cc",
        "src/core/lib/surface/init_secure.cc",
    ],
    hdrs = [
        "src/core/lib/security/context/security_context.h",
        "src/core/lib/security/credentials/alts/alts_credentials.h",
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
        "src/core/lib/security/security_connector/alts_security_connector.h",
        "src/core/lib/security/security_connector/security_connector.h",
        "src/core/lib/security/transport/auth_filters.h",
        "src/core/lib/security/transport/secure_endpoint.h",
        "src/core/lib/security/transport/security_handshaker.h",
        "src/core/lib/security/transport/target_authority_table.h",
        "src/core/lib/security/transport/tsi_error.h",
        "src/core/lib/security/util/json_util.h",
    ],
    language = "c++",
    public_hdrs = GRPC_SECURE_PUBLIC_HDRS,
    deps = [
        "alts_util",
        "grpc_base",
        "grpc_transport_chttp2_alpn",
        "tsi",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.cc",
        "src/core/ext/transport/chttp2/transport/bin_encoder.cc",
        "src/core/ext/transport/chttp2/transport/chttp2_plugin.cc",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.cc",
        "src/core/ext/transport/chttp2/transport/flow_control.cc",
        "src/core/ext/transport/chttp2/transport/frame_data.cc",
        "src/core/ext/transport/chttp2/transport/frame_goaway.cc",
        "src/core/ext/transport/chttp2/transport/frame_ping.cc",
        "src/core/ext/transport/chttp2/transport/frame_rst_stream.cc",
        "src/core/ext/transport/chttp2/transport/frame_settings.cc",
        "src/core/ext/transport/chttp2/transport/frame_window_update.cc",
        "src/core/ext/transport/chttp2/transport/hpack_encoder.cc",
        "src/core/ext/transport/chttp2/transport/hpack_parser.cc",
        "src/core/ext/transport/chttp2/transport/hpack_table.cc",
        "src/core/ext/transport/chttp2/transport/http2_settings.cc",
        "src/core/ext/transport/chttp2/transport/huffsyms.cc",
        "src/core/ext/transport/chttp2/transport/incoming_metadata.cc",
        "src/core/ext/transport/chttp2/transport/parsing.cc",
        "src/core/ext/transport/chttp2/transport/stream_lists.cc",
        "src/core/ext/transport/chttp2/transport/stream_map.cc",
        "src/core/ext/transport/chttp2/transport/varint.cc",
        "src/core/ext/transport/chttp2/transport/writing.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.h",
        "src/core/ext/transport/chttp2/transport/bin_encoder.h",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.h",
        "src/core/ext/transport/chttp2/transport/flow_control.h",
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
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_http_filters",
        "grpc_transport_chttp2_alpn",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_alpn",
    srcs = [
        "src/core/ext/transport/chttp2/alpn/alpn.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/alpn/alpn.h",
    ],
    language = "c++",
    deps = [
        "gpr",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_connector",
    srcs = [
        "src/core/ext/transport/chttp2/client/authority.cc",
        "src/core/ext/transport/chttp2/client/chttp2_connector.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/client/authority.h",
        "src/core/ext/transport/chttp2/client/chttp2_connector.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_insecure",
    srcs = [
        "src/core/ext/transport/chttp2/client/insecure/channel_create.cc",
        "src/core/ext/transport/chttp2/client/insecure/channel_create_posix.cc",
    ],
    language = "c++",
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
        "src/core/ext/transport/chttp2/client/secure/secure_channel_create.cc",
    ],
    language = "c++",
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
        "src/core/ext/transport/chttp2/server/chttp2_server.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/server/chttp2_server.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server_insecure",
    srcs = [
        "src/core/ext/transport/chttp2/server/insecure/server_chttp2.cc",
        "src/core/ext/transport/chttp2/server/insecure/server_chttp2_posix.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_transport_chttp2",
        "grpc_transport_chttp2_server",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server_secure",
    srcs = [
        "src/core/ext/transport/chttp2/server/secure/server_secure_chttp2.cc",
    ],
    language = "c++",
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
        "src/core/ext/transport/cronet/client/secure/cronet_channel_create.cc",
        "src/core/ext/transport/cronet/transport/cronet_api_dummy.cc",
        "src/core/ext/transport/cronet/transport/cronet_transport.cc",
    ],
    hdrs = [
        "src/core/ext/transport/cronet/transport/cronet_transport.h",
        "third_party/objective_c/Cronet/bidirectional_stream_c.h",
    ],
    language = "c++",
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
        "src/core/ext/transport/inproc/inproc_plugin.cc",
        "src/core/ext/transport/inproc/inproc_transport.cc",
    ],
    hdrs = [
        "src/core/ext/transport/inproc/inproc_transport.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "tsi_interface",
    srcs = [
        "src/core/tsi/transport_security.cc",
        "src/core/tsi/transport_security_adapter.cc",
    ],
    hdrs = [
        "src/core/tsi/transport_security.h",
        "src/core/tsi/transport_security_adapter.h",
        "src/core/tsi/transport_security_interface.h",
    ],
    language = "c++",
    deps = [
        "gpr",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "alts_frame_protector",
    srcs = [
        "src/core/tsi/alts/crypt/aes_gcm.cc",
        "src/core/tsi/alts/crypt/gsec.cc",
        "src/core/tsi/alts/frame_protector/alts_counter.cc",
        "src/core/tsi/alts/frame_protector/alts_crypter.cc",
        "src/core/tsi/alts/frame_protector/alts_frame_protector.cc",
        "src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc",
        "src/core/tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc",
        "src/core/tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc",
        "src/core/tsi/alts/frame_protector/frame_handler.cc",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc",
    ],
    hdrs = [
        "src/core/tsi/alts/crypt/gsec.h",
        "src/core/tsi/alts/frame_protector/alts_counter.h",
        "src/core/tsi/alts/frame_protector/alts_crypter.h",
        "src/core/tsi/alts/frame_protector/alts_frame_protector.h",
        "src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.h",
        "src/core/tsi/alts/frame_protector/frame_handler.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h",
        "src/core/tsi/transport_security_grpc.h",
    ],
    external_deps = [
        "libssl",
    ],
    language = "c++",
    deps = [
        "gpr",
        "grpc_base",
        "tsi_interface",
    ],
)

grpc_cc_library(
    name = "alts_proto",
    srcs = [
        "src/core/tsi/alts/handshaker/altscontext.pb.c",
        "src/core/tsi/alts/handshaker/handshaker.pb.c",
        "src/core/tsi/alts/handshaker/transport_security_common.pb.c",
    ],
    hdrs = [
        "src/core/tsi/alts/handshaker/altscontext.pb.h",
        "src/core/tsi/alts/handshaker/handshaker.pb.h",
        "src/core/tsi/alts/handshaker/transport_security_common.pb.h",
    ],
    external_deps = [
        "nanopb",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "alts_util",
    srcs = [
        "src/core/lib/security/credentials/alts/check_gcp_environment.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc",
        "src/core/tsi/alts/handshaker/alts_handshaker_service_api.cc",
        "src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.cc",
        "src/core/tsi/alts/handshaker/transport_security_common_api.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/alts/check_gcp_environment.h",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h",
        "src/core/tsi/alts/handshaker/alts_handshaker_service_api.h",
        "src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.h",
        "src/core/tsi/alts/handshaker/transport_security_common_api.h",
    ],
    public_hdrs = GRPC_SECURE_PUBLIC_HDRS, 
    external_deps = [
        "nanopb",
    ],
    language = "c++",
    deps = [
        "alts_proto",
        "gpr",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "tsi",
    srcs = [
        "src/core/tsi/alts/handshaker/alts_handshaker_client.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_event.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.cc",
        "src/core/tsi/alts_transport_security.cc",
        "src/core/tsi/fake_transport_security.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_openssl.cc",
        "src/core/tsi/ssl_transport_security.cc",
        "src/core/tsi/transport_security_grpc.cc",
    ],
    hdrs = [
        "src/core/tsi/alts/handshaker/alts_handshaker_client.h",
        "src/core/tsi/alts/handshaker/alts_tsi_event.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.h",
        "src/core/tsi/alts_transport_security.h",
        "src/core/tsi/fake_transport_security.h",
        "src/core/tsi/ssl/session_cache/ssl_session.h",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.h",
        "src/core/tsi/ssl_transport_security.h",
        "src/core/tsi/ssl_types.h",
        "src/core/tsi/transport_security_grpc.h",
    ],
    external_deps = [
        "libssl",
    ],
    language = "c++",
    deps = [
        "alts_frame_protector",
        "alts_util",
        "gpr",
        "grpc_base",
        "grpc_transport_chttp2_client_insecure",
        "tsi_interface",
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
        "include/grpc++/impl/codegen/byte_buffer.h",
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
        "include/grpcpp/impl/codegen/async_stream.h",
        "include/grpcpp/impl/codegen/async_unary_call.h",
        "include/grpcpp/impl/codegen/byte_buffer.h",
        "include/grpcpp/impl/codegen/call.h",
        "include/grpcpp/impl/codegen/call_hook.h",
        "include/grpcpp/impl/codegen/channel_interface.h",
        "include/grpcpp/impl/codegen/client_context.h",
        "include/grpcpp/impl/codegen/client_unary_call.h",
        "include/grpcpp/impl/codegen/completion_queue.h",
        "include/grpcpp/impl/codegen/completion_queue_tag.h",
        "include/grpcpp/impl/codegen/config.h",
        "include/grpcpp/impl/codegen/core_codegen_interface.h",
        "include/grpcpp/impl/codegen/create_auth_context.h",
        "include/grpcpp/impl/codegen/grpc_library.h",
        "include/grpcpp/impl/codegen/metadata_map.h",
        "include/grpcpp/impl/codegen/method_handler_impl.h",
        "include/grpcpp/impl/codegen/rpc_method.h",
        "include/grpcpp/impl/codegen/rpc_service_method.h",
        "include/grpcpp/impl/codegen/security/auth_context.h",
        "include/grpcpp/impl/codegen/serialization_traits.h",
        "include/grpcpp/impl/codegen/server_context.h",
        "include/grpcpp/impl/codegen/server_interface.h",
        "include/grpcpp/impl/codegen/service_type.h",
        "include/grpcpp/impl/codegen/slice.h",
        "include/grpcpp/impl/codegen/status.h",
        "include/grpcpp/impl/codegen/status_code_enum.h",
        "include/grpcpp/impl/codegen/string_ref.h",
        "include/grpcpp/impl/codegen/stub_options.h",
        "include/grpcpp/impl/codegen/sync_stream.h",
        "include/grpcpp/impl/codegen/time.h",
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
        "include/grpcpp/impl/codegen/proto_buffer_reader.h",
        "include/grpcpp/impl/codegen/proto_buffer_writer.h",
        "include/grpcpp/impl/codegen/proto_utils.h",
    ],
    deps = [
        "grpc++_codegen_base",
        "grpc++_config_proto",
    ],
)

grpc_cc_library(
    name = "grpc++_config_proto",
    external_deps = [
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/config_protobuf.h",
        "include/grpcpp/impl/codegen/config_protobuf.h",
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
        "include/grpcpp/ext/proto_server_reflection_plugin.h",
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
        "include/grpcpp/test/mock_stream.h",
        "include/grpcpp/test/server_context_test_spouse.h",
    ],
    deps = [
        ":grpc++",
    ],
)

grpc_cc_library(
    name = "grpc_server_backward_compatibility",
    srcs = [
        "src/core/ext/filters/workarounds/workaround_utils.cc",
    ],
    hdrs = [
        "src/core/ext/filters/workarounds/workaround_utils.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc++_core_stats",
    srcs = [
        "src/cpp/util/core_stats.cc",
    ],
    hdrs = [
        "src/cpp/util/core_stats.h",
    ],
    language = "c++",
    deps = [
        ":grpc++",
        "//src/proto/grpc/core:stats_proto",
    ],
)

grpc_generate_one_off_targets()
