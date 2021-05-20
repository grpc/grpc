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

licenses(["notice"])

exports_files([
    "LICENSE",
    "etc/roots.pem",
])

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
    "grpc_generate_one_off_targets",
    "grpc_upb_proto_library",
    "python_config_settings",
)

config_setting(
    name = "grpc_no_ares",
    values = {"define": "grpc_no_ares=true"},
)

config_setting(
    name = "grpc_no_xds",
    values = {"define": "grpc_no_xds=true"},
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

config_setting(
    name = "mac_x86_64",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "use_strict_warning",
    values = {"define": "use_strict_warning=true"},
)

python_config_settings()

# This should be updated along with build_handwritten.yaml
g_stands_for = "guadalupe_river_park_conservancy"  # @unused

core_version = "16.0.0"  # @unused

version = "1.38.0"  # @unused

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
    "include/grpc/support/sync_abseil.h",
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
    "include/grpc/slice.h",
    "include/grpc/slice_buffer.h",
    "include/grpc/status.h",
    "include/grpc/load_reporting.h",
    "include/grpc/support/workaround_list.h",
]

GRPC_PUBLIC_EVENT_ENGINE_HDRS = [
    "include/grpc/event_engine/channel_args.h",
    "include/grpc/event_engine/event_engine.h",
    "include/grpc/event_engine/port.h",
    "include/grpc/event_engine/slice_allocator.h",
]

GRPC_SECURE_PUBLIC_HDRS = [
    "include/grpc/grpc_security.h",
]

# TODO(ctiller): layer grpc atop grpc_unsecure, layer grpc++ atop grpc++_unsecure
GRPCXX_SRCS = [
    "src/cpp/client/channel_cc.cc",
    "src/cpp/client/client_callback.cc",
    "src/cpp/client/client_context.cc",
    "src/cpp/client/client_interceptor.cc",
    "src/cpp/client/create_channel.cc",
    "src/cpp/client/create_channel_internal.cc",
    "src/cpp/client/create_channel_posix.cc",
    "src/cpp/client/credentials_cc.cc",
    "src/cpp/common/alarm.cc",
    "src/cpp/common/channel_arguments.cc",
    "src/cpp/common/channel_filter.cc",
    "src/cpp/common/completion_queue_cc.cc",
    "src/cpp/common/core_codegen.cc",
    "src/cpp/common/resource_quota_cc.cc",
    "src/cpp/common/rpc_method.cc",
    "src/cpp/common/version_cc.cc",
    "src/cpp/common/validate_service_config.cc",
    "src/cpp/server/async_generic_service.cc",
    "src/cpp/server/channel_argument_option.cc",
    "src/cpp/server/create_default_thread_pool.cc",
    "src/cpp/server/dynamic_thread_pool.cc",
    "src/cpp/server/external_connection_acceptor_impl.cc",
    "src/cpp/server/health/default_health_check_service.cc",
    "src/cpp/server/health/health_check_service.cc",
    "src/cpp/server/health/health_check_service_server_builder_option.cc",
    "src/cpp/server/server_builder.cc",
    "src/cpp/server/server_callback.cc",
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
    "src/cpp/server/external_connection_acceptor_impl.h",
    "src/cpp/server/health/default_health_check_service.h",
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
    "include/grpc++/security/auth_context.h",
    "include/grpc++/resource_quota.h",
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
    "include/grpcpp/resource_quota.h",
    "include/grpcpp/security/auth_context.h",
    "include/grpcpp/security/auth_metadata_processor.h",
    "include/grpcpp/security/credentials.h",
    "include/grpcpp/security/server_credentials.h",
    "include/grpcpp/security/tls_certificate_provider.h",
    "include/grpcpp/security/tls_credentials_options.h",
    "include/grpcpp/server.h",
    "include/grpcpp/server_builder.h",
    "include/grpcpp/server_context.h",
    "include/grpcpp/server_posix.h",
    "include/grpcpp/support/async_stream.h",
    "include/grpcpp/support/async_unary_call.h",
    "include/grpcpp/support/byte_buffer.h",
    "include/grpcpp/support/channel_arguments.h",
    "include/grpcpp/support/client_callback.h",
    "include/grpcpp/support/client_interceptor.h",
    "include/grpcpp/support/config.h",
    "include/grpcpp/support/interceptor.h",
    "include/grpcpp/support/message_allocator.h",
    "include/grpcpp/support/method_handler.h",
    "include/grpcpp/support/proto_buffer_reader.h",
    "include/grpcpp/support/proto_buffer_writer.h",
    "include/grpcpp/support/server_callback.h",
    "include/grpcpp/support/server_interceptor.h",
    "include/grpcpp/support/slice.h",
    "include/grpcpp/support/status.h",
    "include/grpcpp/support/status_code_enum.h",
    "include/grpcpp/support/string_ref.h",
    "include/grpcpp/support/stub_options.h",
    "include/grpcpp/support/sync_stream.h",
    "include/grpcpp/support/time.h",
    "include/grpcpp/support/validate_service_config.h",
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
    defines = select({
        "grpc_no_xds": ["GRPC_NO_XDS"],
        "//conditions:default": [],
    }),
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_SECURE_PUBLIC_HDRS,
    select_deps = {
        "grpc_no_xds": [],
        "//conditions:default": [
            "grpc_lb_policy_cds",
            "grpc_lb_policy_xds_cluster_impl",
            "grpc_lb_policy_xds_cluster_manager",
            "grpc_lb_policy_xds_cluster_resolver",
            "grpc_resolver_xds",
            "grpc_resolver_c2p",
            "grpc_xds_server_config_fetcher",
        ],
    },
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
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
)

grpc_cc_library(
    name = "grpc++",
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/common/tls_credentials_options_util.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    select_deps = {
        "grpc_no_xds": [],
        "//conditions:default": [
            "grpc++_xds_client",
            "grpc++_xds_server",
        ],
    },
    standalone = True,
    deps = [
        "grpc++_internals",
    ],
)

grpc_cc_library(
    name = "grpc++_internals",
    srcs = [
        "src/cpp/client/insecure_credentials.cc",
        "src/cpp/client/secure_credentials.cc",
        "src/cpp/common/auth_property_iterator.cc",
        "src/cpp/common/secure_auth_context.cc",
        "src/cpp/common/secure_channel_arguments.cc",
        "src/cpp/common/secure_create_auth_context.cc",
        "src/cpp/common/tls_certificate_provider.cc",
        "src/cpp/common/tls_credentials_options.cc",
        "src/cpp/common/tls_credentials_options_util.cc",
        "src/cpp/server/insecure_server_credentials.cc",
        "src/cpp/server/secure_server_credentials.cc",
    ],
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/common/tls_credentials_options_util.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "gpr",
        "grpc",
        "grpc++_base",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
        "grpc_secure",
    ],
)

grpc_cc_library(
    name = "grpc++_xds_client",
    srcs = [
        "src/cpp/client/xds_credentials.cc",
    ],
    hdrs = [
        "src/cpp/client/secure_credentials.h",
    ],
    language = "c++",
    deps = [
        "grpc++_internals",
    ],
)

grpc_cc_library(
    name = "grpc++_xds_server",
    srcs = [
        "src/cpp/server/xds_server_credentials.cc",
    ],
    hdrs = [
        "src/cpp/server/secure_server_credentials.h",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/xds_server_builder.h",
    ],
    deps = [
        "grpc++_internals",
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
    ],
)

grpc_cc_library(
    name = "grpc++_alts",
    srcs = [
        "src/cpp/common/alts_context.cc",
        "src/cpp/common/alts_util.cc",
    ],
    hdrs = [
        "include/grpcpp/security/alts_context.h",
        "include/grpcpp/security/alts_util.h",
    ],
    language = "c++",
    standalone = True,
    deps = [
        "alts_upb",
        "alts_util",
        "grpc++",
    ],
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
        "src/core/ext/filters/census/grpc_context.cc",
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
    name = "grpc++_internal_hdrs_only",
    hdrs = [
        "include/grpcpp/impl/codegen/sync.h",
    ],
    external_deps = [
        "absl/synchronization",
    ],
    language = "c++",
    deps = [
        "gpr_codegen",
    ],
)

grpc_cc_library(
    name = "gpr_base",
    srcs = [
        "src/core/lib/gpr/alloc.cc",
        "src/core/lib/gpr/atm.cc",
        "src/core/lib/gpr/cpu_iphone.cc",
        "src/core/lib/gpr/cpu_linux.cc",
        "src/core/lib/gpr/cpu_posix.cc",
        "src/core/lib/gpr/cpu_windows.cc",
        "src/core/lib/gpr/env_linux.cc",
        "src/core/lib/gpr/env_posix.cc",
        "src/core/lib/gpr/env_windows.cc",
        "src/core/lib/gpr/log.cc",
        "src/core/lib/gpr/log_android.cc",
        "src/core/lib/gpr/log_linux.cc",
        "src/core/lib/gpr/log_posix.cc",
        "src/core/lib/gpr/log_windows.cc",
        "src/core/lib/gpr/murmur_hash.cc",
        "src/core/lib/gpr/string.cc",
        "src/core/lib/gpr/string_posix.cc",
        "src/core/lib/gpr/string_util_windows.cc",
        "src/core/lib/gpr/string_windows.cc",
        "src/core/lib/gpr/sync.cc",
        "src/core/lib/gpr/sync_abseil.cc",
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
        "src/core/lib/gprpp/arena.cc",
        "src/core/lib/gprpp/examine_stack.cc",
        "src/core/lib/gprpp/fork.cc",
        "src/core/lib/gprpp/global_config_env.cc",
        "src/core/lib/gprpp/host_port.cc",
        "src/core/lib/gprpp/mpscq.cc",
        "src/core/lib/gprpp/stat_posix.cc",
        "src/core/lib/gprpp/stat_windows.cc",
        "src/core/lib/gprpp/status_helper.cc",
        "src/core/lib/gprpp/thd_posix.cc",
        "src/core/lib/gprpp/thd_windows.cc",
        "src/core/lib/gprpp/time_util.cc",
        "src/core/lib/profiling/basic_timers.cc",
        "src/core/lib/profiling/stap_timers.cc",
    ],
    hdrs = [
        "src/core/lib/gpr/alloc.h",
        "src/core/lib/gpr/arena.h",
        "src/core/lib/gpr/env.h",
        "src/core/lib/gpr/murmur_hash.h",
        "src/core/lib/gpr/spinlock.h",
        "src/core/lib/gpr/string.h",
        "src/core/lib/gpr/string_windows.h",
        "src/core/lib/gpr/time_precise.h",
        "src/core/lib/gpr/tls.h",
        "src/core/lib/gpr/tls_gcc.h",
        "src/core/lib/gpr/tls_msvc.h",
        "src/core/lib/gpr/tls_pthread.h",
        "src/core/lib/gpr/tls_stdcpp.h",
        "src/core/lib/gpr/tmpfile.h",
        "src/core/lib/gpr/useful.h",
        "src/core/lib/gprpp/arena.h",
        "src/core/lib/gprpp/atomic.h",
        "src/core/lib/gprpp/examine_stack.h",
        "src/core/lib/gprpp/fork.h",
        "src/core/lib/gprpp/global_config.h",
        "src/core/lib/gprpp/global_config_custom.h",
        "src/core/lib/gprpp/global_config_env.h",
        "src/core/lib/gprpp/global_config_generic.h",
        "src/core/lib/gprpp/host_port.h",
        "src/core/lib/gprpp/manual_constructor.h",
        "src/core/lib/gprpp/memory.h",
        "src/core/lib/gprpp/mpscq.h",
        "src/core/lib/gprpp/stat.h",
        "src/core/lib/gprpp/status_helper.h",
        "src/core/lib/gprpp/sync.h",
        "src/core/lib/gprpp/thd.h",
        "src/core/lib/gprpp/time_util.h",
        "src/core/lib/profiling/timers.h",
    ],
    external_deps = [
        "absl/base",
        "absl/memory",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/time:time",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    deps = [
        "debug_location",
        "google_api_upb",
        "gpr_codegen",
        "grpc_codegen",
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
        "include/grpc/impl/codegen/log.h",
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/impl/codegen/sync.h",
        "include/grpc/impl/codegen/sync_abseil.h",
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
    language = "c++",
    public_hdrs = [
        "src/core/lib/gprpp/atomic.h",
    ],
    deps = [
        "gpr",
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
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "ref_counted",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/ref_counted.h"],
    deps = [
        "atomic",
        "debug_location",
        "gpr_base",
        "grpc_trace",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "dual_ref_counted",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/dual_ref_counted.h"],
    deps = [
        "atomic",
        "debug_location",
        "gpr_base",
        "grpc_trace",
        "orphanable",
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
        "src/core/lib/address_utils/parse_address.cc",
        "src/core/lib/address_utils/sockaddr_utils.cc",
        "src/core/lib/avl/avl.cc",
        "src/core/lib/backoff/backoff.cc",
        "src/core/lib/channel/channel_args.cc",
        "src/core/lib/channel/channel_stack.cc",
        "src/core/lib/channel/channel_stack_builder.cc",
        "src/core/lib/channel/channel_trace.cc",
        "src/core/lib/channel/channelz.cc",
        "src/core/lib/channel/channelz_registry.cc",
        "src/core/lib/channel/connected_channel.cc",
        "src/core/lib/channel/handshaker.cc",
        "src/core/lib/channel/handshaker_registry.cc",
        "src/core/lib/channel/status_util.cc",
        "src/core/lib/compression/compression.cc",
        "src/core/lib/compression/compression_args.cc",
        "src/core/lib/compression/compression_internal.cc",
        "src/core/lib/compression/message_compress.cc",
        "src/core/lib/compression/stream_compression.cc",
        "src/core/lib/compression/stream_compression_gzip.cc",
        "src/core/lib/compression/stream_compression_identity.cc",
        "src/core/lib/debug/stats.cc",
        "src/core/lib/debug/stats_data.cc",
        "src/core/lib/event_engine/slice_allocator.cc",
        "src/core/lib/event_engine/sockaddr.cc",
        "src/core/lib/http/format_request.cc",
        "src/core/lib/http/httpcli.cc",
        "src/core/lib/http/parser.cc",
        "src/core/lib/iomgr/buffer_list.cc",
        "src/core/lib/iomgr/call_combiner.cc",
        "src/core/lib/iomgr/cfstream_handle.cc",
        "src/core/lib/iomgr/combiner.cc",
        "src/core/lib/iomgr/dualstack_socket_posix.cc",
        "src/core/lib/iomgr/endpoint.cc",
        "src/core/lib/iomgr/endpoint_cfstream.cc",
        "src/core/lib/iomgr/endpoint_pair_posix.cc",
        "src/core/lib/iomgr/endpoint_pair_uv.cc",
        "src/core/lib/iomgr/endpoint_pair_windows.cc",
        "src/core/lib/iomgr/error.cc",
        "src/core/lib/iomgr/error_cfstream.cc",
        "src/core/lib/iomgr/ev_apple.cc",
        "src/core/lib/iomgr/ev_epoll1_linux.cc",
        "src/core/lib/iomgr/ev_epollex_linux.cc",
        "src/core/lib/iomgr/ev_poll_posix.cc",
        "src/core/lib/iomgr/ev_posix.cc",
        "src/core/lib/iomgr/ev_windows.cc",
        "src/core/lib/iomgr/exec_ctx.cc",
        "src/core/lib/iomgr/executor.cc",
        "src/core/lib/iomgr/executor/mpmcqueue.cc",
        "src/core/lib/iomgr/executor/threadpool.cc",
        "src/core/lib/iomgr/fork_posix.cc",
        "src/core/lib/iomgr/fork_windows.cc",
        "src/core/lib/iomgr/gethostname_fallback.cc",
        "src/core/lib/iomgr/gethostname_host_name_max.cc",
        "src/core/lib/iomgr/gethostname_sysconf.cc",
        "src/core/lib/iomgr/grpc_if_nametoindex_posix.cc",
        "src/core/lib/iomgr/grpc_if_nametoindex_unsupported.cc",
        "src/core/lib/iomgr/internal_errqueue.cc",
        "src/core/lib/iomgr/iocp_windows.cc",
        "src/core/lib/iomgr/iomgr.cc",
        "src/core/lib/iomgr/iomgr_custom.cc",
        "src/core/lib/iomgr/iomgr_internal.cc",
        "src/core/lib/iomgr/iomgr_posix.cc",
        "src/core/lib/iomgr/iomgr_posix_cfstream.cc",
        "src/core/lib/iomgr/iomgr_uv.cc",
        "src/core/lib/iomgr/iomgr_windows.cc",
        "src/core/lib/iomgr/is_epollexclusive_available.cc",
        "src/core/lib/iomgr/load_file.cc",
        "src/core/lib/iomgr/lockfree_event.cc",
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
        "src/core/lib/iomgr/socket_factory_posix.cc",
        "src/core/lib/iomgr/socket_mutator.cc",
        "src/core/lib/iomgr/socket_utils_common_posix.cc",
        "src/core/lib/iomgr/socket_utils_linux.cc",
        "src/core/lib/iomgr/socket_utils_posix.cc",
        "src/core/lib/iomgr/socket_utils_uv.cc",
        "src/core/lib/iomgr/socket_utils_windows.cc",
        "src/core/lib/iomgr/socket_windows.cc",
        "src/core/lib/iomgr/tcp_client.cc",
        "src/core/lib/iomgr/tcp_client_cfstream.cc",
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
        "src/core/lib/iomgr/wakeup_fd_eventfd.cc",
        "src/core/lib/iomgr/wakeup_fd_nospecial.cc",
        "src/core/lib/iomgr/wakeup_fd_pipe.cc",
        "src/core/lib/iomgr/wakeup_fd_posix.cc",
        "src/core/lib/iomgr/work_serializer.cc",
        "src/core/lib/json/json_reader.cc",
        "src/core/lib/json/json_util.cc",
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
        "src/core/lib/transport/authority_override.cc",
        "src/core/lib/transport/bdp_estimator.cc",
        "src/core/lib/transport/byte_stream.cc",
        "src/core/lib/transport/connectivity_state.cc",
        "src/core/lib/transport/error_utils.cc",
        "src/core/lib/transport/metadata.cc",
        "src/core/lib/transport/metadata_batch.cc",
        "src/core/lib/transport/pid_controller.cc",
        "src/core/lib/transport/static_metadata.cc",
        "src/core/lib/transport/status_conversion.cc",
        "src/core/lib/transport/status_metadata.cc",
        "src/core/lib/transport/timeout_encoding.cc",
        "src/core/lib/transport/transport.cc",
        "src/core/lib/transport/transport_op_string.cc",
        "src/core/lib/uri/uri_parser.cc",
    ],
    hdrs = [
        "src/core/lib/address_utils/parse_address.h",
        "src/core/lib/address_utils/sockaddr_utils.h",
        "src/core/lib/avl/avl.h",
        "src/core/lib/backoff/backoff.h",
        "src/core/lib/channel/channel_args.h",
        "src/core/lib/channel/channel_stack.h",
        "src/core/lib/channel/channel_stack_builder.h",
        "src/core/lib/channel/channel_trace.h",
        "src/core/lib/channel/channelz.h",
        "src/core/lib/channel/channelz_registry.h",
        "src/core/lib/channel/connected_channel.h",
        "src/core/lib/channel/context.h",
        "src/core/lib/channel/handshaker.h",
        "src/core/lib/channel/handshaker_factory.h",
        "src/core/lib/channel/handshaker_registry.h",
        "src/core/lib/channel/status_util.h",
        "src/core/lib/compression/algorithm_metadata.h",
        "src/core/lib/compression/compression_args.h",
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
        "src/core/lib/iomgr/buffer_list.h",
        "src/core/lib/iomgr/call_combiner.h",
        "src/core/lib/iomgr/cfstream_handle.h",
        "src/core/lib/iomgr/closure.h",
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/dynamic_annotations.h",
        "src/core/lib/iomgr/endpoint.h",
        "src/core/lib/iomgr/endpoint_cfstream.h",
        "src/core/lib/iomgr/endpoint_pair.h",
        "src/core/lib/iomgr/error.h",
        "src/core/lib/iomgr/error_cfstream.h",
        "src/core/lib/iomgr/error_internal.h",
        "src/core/lib/iomgr/ev_apple.h",
        "src/core/lib/iomgr/ev_epoll1_linux.h",
        "src/core/lib/iomgr/ev_epollex_linux.h",
        "src/core/lib/iomgr/ev_poll_posix.h",
        "src/core/lib/iomgr/ev_posix.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/executor/mpmcqueue.h",
        "src/core/lib/iomgr/executor/threadpool.h",
        "src/core/lib/iomgr/gethostname.h",
        "src/core/lib/iomgr/grpc_if_nametoindex.h",
        "src/core/lib/iomgr/internal_errqueue.h",
        "src/core/lib/iomgr/iocp_windows.h",
        "src/core/lib/iomgr/iomgr.h",
        "src/core/lib/iomgr/iomgr_custom.h",
        "src/core/lib/iomgr/iomgr_internal.h",
        "src/core/lib/iomgr/is_epollexclusive_available.h",
        "src/core/lib/iomgr/load_file.h",
        "src/core/lib/iomgr/lockfree_event.h",
        "src/core/lib/iomgr/nameser.h",
        "src/core/lib/iomgr/polling_entity.h",
        "src/core/lib/iomgr/pollset.h",
        "src/core/lib/iomgr/pollset_custom.h",
        "src/core/lib/iomgr/pollset_set.h",
        "src/core/lib/iomgr/pollset_set_custom.h",
        "src/core/lib/iomgr/pollset_set_windows.h",
        "src/core/lib/iomgr/pollset_uv.h",
        "src/core/lib/iomgr/pollset_windows.h",
        "src/core/lib/iomgr/port.h",
        "src/core/lib/iomgr/python_util.h",
        "src/core/lib/iomgr/resolve_address.h",
        "src/core/lib/iomgr/resolve_address_custom.h",
        "src/core/lib/iomgr/resource_quota.h",
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_custom.h",
        "src/core/lib/iomgr/sockaddr_posix.h",
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
        "src/core/lib/iomgr/wakeup_fd_pipe.h",
        "src/core/lib/iomgr/wakeup_fd_posix.h",
        "src/core/lib/iomgr/work_serializer.h",
        "src/core/lib/json/json.h",
        "src/core/lib/json/json_util.h",
        "src/core/lib/slice/b64.h",
        "src/core/lib/slice/percent_encoding.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
        "src/core/lib/slice/slice_utils.h",
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
        "src/core/lib/transport/authority_override.h",
        "src/core/lib/transport/bdp_estimator.h",
        "src/core/lib/transport/byte_stream.h",
        "src/core/lib/transport/connectivity_state.h",
        "src/core/lib/transport/error_utils.h",
        "src/core/lib/transport/http2_errors.h",
        "src/core/lib/transport/metadata.h",
        "src/core/lib/transport/metadata_batch.h",
        "src/core/lib/transport/pid_controller.h",
        "src/core/lib/transport/static_metadata.h",
        "src/core/lib/transport/status_conversion.h",
        "src/core/lib/transport/status_metadata.h",
        "src/core/lib/transport/timeout_encoding.h",
        "src/core/lib/transport/transport.h",
        "src/core/lib/transport/transport_impl.h",
        "src/core/lib/uri/uri_parser.h",
    ],
    external_deps = [
        "madler_zlib",
        "absl/container:inlined_vector",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/container:flat_hash_map",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    deps = [
        "dual_ref_counted",
        "gpr_base",
        "grpc_codegen",
        "grpc_trace",
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
        "grpc_lb_policy_priority",
        "grpc_lb_policy_round_robin",
        "grpc_lb_policy_weighted_target",
        "grpc_client_idle_filter",
        "grpc_max_age_filter",
        "grpc_message_size_filter",
        "grpc_resolver_dns_ares",
        "grpc_resolver_fake",
        "grpc_resolver_dns_native",
        "grpc_resolver_sockaddr",
        "grpc_transport_chttp2_client_insecure",
        "grpc_transport_chttp2_server_insecure",
        "grpc_transport_inproc",
        "grpc_fault_injection_filter",
        "grpc_workaround_cronet_compression_filter",
        "grpc_server_backward_compatibility",
    ],
)

grpc_cc_library(
    name = "grpc_client_channel",
    srcs = [
        "src/core/ext/filters/client_channel/backend_metric.cc",
        "src/core/ext/filters/client_channel/backup_poller.cc",
        "src/core/ext/filters/client_channel/channel_connectivity.cc",
        "src/core/ext/filters/client_channel/client_channel.cc",
        "src/core/ext/filters/client_channel/client_channel_channelz.cc",
        "src/core/ext/filters/client_channel/client_channel_factory.cc",
        "src/core/ext/filters/client_channel/client_channel_plugin.cc",
        "src/core/ext/filters/client_channel/config_selector.cc",
        "src/core/ext/filters/client_channel/dynamic_filters.cc",
        "src/core/ext/filters/client_channel/global_subchannel_pool.cc",
        "src/core/ext/filters/client_channel/health/health_check_client.cc",
        "src/core/ext/filters/client_channel/http_connect_handshaker.cc",
        "src/core/ext/filters/client_channel/http_proxy.cc",
        "src/core/ext/filters/client_channel/lb_policy.cc",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc",
        "src/core/ext/filters/client_channel/lb_policy_registry.cc",
        "src/core/ext/filters/client_channel/local_subchannel_pool.cc",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.cc",
        "src/core/ext/filters/client_channel/resolver.cc",
        "src/core/ext/filters/client_channel/resolver_registry.cc",
        "src/core/ext/filters/client_channel/resolver_result_parsing.cc",
        "src/core/ext/filters/client_channel/retry_filter.cc",
        "src/core/ext/filters/client_channel/retry_service_config.cc",
        "src/core/ext/filters/client_channel/retry_throttle.cc",
        "src/core/ext/filters/client_channel/server_address.cc",
        "src/core/ext/filters/client_channel/service_config.cc",
        "src/core/ext/filters/client_channel/service_config_channel_arg_filter.cc",
        "src/core/ext/filters/client_channel/service_config_parser.cc",
        "src/core/ext/filters/client_channel/subchannel.cc",
        "src/core/ext/filters/client_channel/subchannel_pool_interface.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/backend_metric.h",
        "src/core/ext/filters/client_channel/backup_poller.h",
        "src/core/ext/filters/client_channel/client_channel.h",
        "src/core/ext/filters/client_channel/client_channel_channelz.h",
        "src/core/ext/filters/client_channel/client_channel_factory.h",
        "src/core/ext/filters/client_channel/config_selector.h",
        "src/core/ext/filters/client_channel/connector.h",
        "src/core/ext/filters/client_channel/dynamic_filters.h",
        "src/core/ext/filters/client_channel/global_subchannel_pool.h",
        "src/core/ext/filters/client_channel/health/health_check_client.h",
        "src/core/ext/filters/client_channel/http_connect_handshaker.h",
        "src/core/ext/filters/client_channel/http_proxy.h",
        "src/core/ext/filters/client_channel/lb_policy.h",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h",
        "src/core/ext/filters/client_channel/lb_policy_factory.h",
        "src/core/ext/filters/client_channel/lb_policy_registry.h",
        "src/core/ext/filters/client_channel/local_subchannel_pool.h",
        "src/core/ext/filters/client_channel/proxy_mapper.h",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.h",
        "src/core/ext/filters/client_channel/resolver.h",
        "src/core/ext/filters/client_channel/resolver_factory.h",
        "src/core/ext/filters/client_channel/resolver_registry.h",
        "src/core/ext/filters/client_channel/resolver_result_parsing.h",
        "src/core/ext/filters/client_channel/retry_filter.h",
        "src/core/ext/filters/client_channel/retry_service_config.h",
        "src/core/ext/filters/client_channel/retry_throttle.h",
        "src/core/ext/filters/client_channel/server_address.h",
        "src/core/ext/filters/client_channel/service_config.h",
        "src/core/ext/filters/client_channel/service_config_call_data.h",
        "src/core/ext/filters/client_channel/service_config_parser.h",
        "src/core/ext/filters/client_channel/subchannel.h",
        "src/core/ext/filters/client_channel/subchannel_interface.h",
        "src/core/ext/filters/client_channel/subchannel_pool_interface.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_authority_filter",
        "grpc_deadline_filter",
        "grpc_health_upb",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
        "udpa_orca_upb",
    ],
)

grpc_cc_library(
    name = "grpc_client_idle_filter",
    srcs = [
        "src/core/ext/filters/client_idle/client_idle_filter.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
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
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_fault_injection_filter",
    srcs = [
        "src/core/ext/filters/fault_injection/fault_injection_filter.cc",
        "src/core/ext/filters/fault_injection/service_config_parser.cc",
    ],
    hdrs = [
        "src/core/ext/filters/fault_injection/fault_injection_filter.h",
        "src/core/ext/filters/fault_injection/service_config_parser.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
    ],
)

grpc_cc_library(
    name = "grpc_http_filters",
    srcs = [
        "src/core/ext/filters/http/client/http_client_filter.cc",
        "src/core/ext/filters/http/http_filters_plugin.cc",
        "src/core/ext/filters/http/message_compress/message_compress_filter.cc",
        "src/core/ext/filters/http/message_compress/message_decompress_filter.cc",
        "src/core/ext/filters/http/server/http_server_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/http/client/http_client_filter.h",
        "src/core/ext/filters/http/message_compress/message_compress_filter.h",
        "src/core/ext/filters/http/message_compress/message_decompress_filter.h",
        "src/core/ext/filters/http/server/http_server_filter.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_message_size_filter",
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
    name = "grpc_grpclb_balancer_addresses",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
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
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_grpclb_balancer_addresses",
        "grpc_lb_upb",
        "grpc_resolver_fake",
        "grpc_transport_chttp2_client_insecure",
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
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_grpclb_balancer_addresses",
        "grpc_lb_upb",
        "grpc_resolver_fake",
        "grpc_secure",
        "grpc_transport_chttp2_client_secure",
    ],
)

grpc_cc_library(
    name = "grpc_xds_client",
    srcs = [
        "src/core/ext/xds/certificate_provider_registry.cc",
        "src/core/ext/xds/certificate_provider_store.cc",
        "src/core/ext/xds/file_watcher_certificate_provider_factory.cc",
        "src/core/ext/xds/xds_api.cc",
        "src/core/ext/xds/xds_bootstrap.cc",
        "src/core/ext/xds/xds_certificate_provider.cc",
        "src/core/ext/xds/xds_client.cc",
        "src/core/ext/xds/xds_client_stats.cc",
        "src/core/ext/xds/xds_http_fault_filter.cc",
        "src/core/ext/xds/xds_http_filters.cc",
        "src/core/lib/security/credentials/xds/xds_credentials.cc",
    ],
    hdrs = [
        "src/core/ext/xds/certificate_provider_factory.h",
        "src/core/ext/xds/certificate_provider_registry.h",
        "src/core/ext/xds/certificate_provider_store.h",
        "src/core/ext/xds/file_watcher_certificate_provider_factory.h",
        "src/core/ext/xds/xds_api.h",
        "src/core/ext/xds/xds_bootstrap.h",
        "src/core/ext/xds/xds_certificate_provider.h",
        "src/core/ext/xds/xds_channel_args.h",
        "src/core/ext/xds/xds_client.h",
        "src/core/ext/xds/xds_client_stats.h",
        "src/core/ext/xds/xds_http_fault_filter.h",
        "src/core/ext/xds/xds_http_filters.h",
        "src/core/lib/security/credentials/xds/xds_credentials.h",
    ],
    external_deps = [
        "absl/functional:bind_front",
        "upb_lib",
        "upb_textformat_lib",
        "upb_json_lib",
        "re2",
    ],
    language = "c++",
    deps = [
        "envoy_ads_upb",
        "envoy_ads_upbdefs",
        "grpc_base",
        "grpc_client_channel",
        "grpc_fault_injection_filter",
        "grpc_matchers",
        "grpc_secure",
        "grpc_transport_chttp2_client_secure",
        "udpa_type_upb",
        "udpa_type_upbdefs",
    ],
)

grpc_cc_library(
    name = "grpc_xds_server_config_fetcher",
    srcs = [
        "src/core/ext/xds/xds_server_config_fetcher.cc",
    ],
    language = "c++",
    deps = [
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_google_mesh_ca_certificate_provider_factory",
    srcs = [
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.cc",
    ],
    hdrs = [
        "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_cds",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/cds.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_xds_channel_args",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel_args.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "grpc_lb_xds_common",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_resolver",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_resolver.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_resolver_fake",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_impl",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_impl.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_manager",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_manager.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_xds_header",
    ],
)

grpc_cc_library(
    name = "grpc_lb_address_filtering",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/address_filtering.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/address_filtering.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
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
    name = "grpc_lb_policy_ring_hash",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h",
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
    name = "grpc_lb_policy_priority",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/priority/priority.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_target",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
    ],
)

grpc_cc_library(
    name = "lb_server_load_reporting_filter",
    srcs = [
        "src/core/ext/filters/load_reporting/server_load_reporting_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/load_reporting/registered_opencensus_objects.h",
        "src/core/ext/filters/load_reporting/server_load_reporting_filter.h",
        "src/cpp/server/load_reporter/constants.h",
    ],
    external_deps = [
        "opencensus-stats",
    ],
    language = "c++",
    deps = [
        "grpc++_base",
        "grpc_secure",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "lb_load_data_store",
    srcs = [
        "src/cpp/server/load_reporter/load_data_store.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/constants.h",
        "src/cpp/server/load_reporter/load_data_store.h",
    ],
    language = "c++",
    deps = [
        "grpc++",
    ],
)

grpc_cc_library(
    name = "lb_server_load_reporting_service_server_builder_plugin",
    srcs = [
        "src/cpp/server/load_reporter/load_reporting_service_server_builder_plugin.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/load_reporting_service_server_builder_plugin.h",
    ],
    language = "c++",
    deps = [
        "lb_load_reporter_service",
    ],
)

grpc_cc_library(
    name = "grpcpp_server_load_reporting",
    srcs = [
        "src/cpp/server/load_reporter/load_reporting_service_server_builder_option.cc",
        "src/cpp/server/load_reporter/util.cc",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/server_load_reporting.h",
    ],
    deps = [
        "lb_server_load_reporting_filter",
        "lb_server_load_reporting_service_server_builder_plugin",
    ],
)

grpc_cc_library(
    name = "lb_load_reporter_service",
    srcs = [
        "src/cpp/server/load_reporter/load_reporter_async_service_impl.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/load_reporter_async_service_impl.h",
    ],
    language = "c++",
    deps = [
        "lb_load_reporter",
    ],
)

grpc_cc_library(
    name = "lb_get_cpu_stats",
    srcs = [
        "src/cpp/server/load_reporter/get_cpu_stats_linux.cc",
        "src/cpp/server/load_reporter/get_cpu_stats_macos.cc",
        "src/cpp/server/load_reporter/get_cpu_stats_unsupported.cc",
        "src/cpp/server/load_reporter/get_cpu_stats_windows.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/get_cpu_stats.h",
    ],
    language = "c++",
    deps = [
        "grpc++",
    ],
)

grpc_cc_library(
    name = "lb_load_reporter",
    srcs = [
        "src/cpp/server/load_reporter/load_reporter.cc",
    ],
    hdrs = [
        "src/cpp/server/load_reporter/constants.h",
        "src/cpp/server/load_reporter/load_reporter.h",
    ],
    external_deps = [
        "opencensus-stats",
    ],
    language = "c++",
    deps = [
        "lb_get_cpu_stats",
        "lb_load_data_store",
        "//src/proto/grpc/lb/v1:load_reporter_proto",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_selection",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
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
        "grpc_resolver_dns_selection",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_libuv.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_libuv.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc",
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
        "grpc_grpclb_balancer_addresses",
        "grpc_resolver_dns_selection",
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
    name = "grpc_resolver_xds_header",
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/xds/xds_resolver.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "grpc_resolver_xds",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/xds/xds_resolver.cc",
    ],
    external_deps = [
        "xxhash",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_policy_ring_hash",
        "grpc_xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_c2p",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/google_c2p/google_c2p_resolver.cc",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_client_channel",
        "grpc_xds_client",
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
        "src/core/lib/security/credentials/external/aws_external_account_credentials.cc",
        "src/core/lib/security/credentials/external/aws_request_signer.cc",
        "src/core/lib/security/credentials/external/external_account_credentials.cc",
        "src/core/lib/security/credentials/external/file_external_account_credentials.cc",
        "src/core/lib/security/credentials/external/url_external_account_credentials.cc",
        "src/core/lib/security/credentials/fake/fake_credentials.cc",
        "src/core/lib/security/credentials/google_default/credentials_generic.cc",
        "src/core/lib/security/credentials/google_default/google_default_credentials.cc",
        "src/core/lib/security/credentials/iam/iam_credentials.cc",
        "src/core/lib/security/credentials/insecure/insecure_credentials.cc",
        "src/core/lib/security/credentials/jwt/json_token.cc",
        "src/core/lib/security/credentials/jwt/jwt_credentials.cc",
        "src/core/lib/security/credentials/jwt/jwt_verifier.cc",
        "src/core/lib/security/credentials/local/local_credentials.cc",
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.cc",
        "src/core/lib/security/credentials/plugin/plugin_credentials.cc",
        "src/core/lib/security/credentials/ssl/ssl_credentials.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.cc",
        "src/core/lib/security/credentials/tls/tls_credentials.cc",
        "src/core/lib/security/credentials/tls/tls_utils.cc",
        "src/core/lib/security/security_connector/alts/alts_security_connector.cc",
        "src/core/lib/security/security_connector/fake/fake_security_connector.cc",
        "src/core/lib/security/security_connector/insecure/insecure_security_connector.cc",
        "src/core/lib/security/security_connector/load_system_roots_fallback.cc",
        "src/core/lib/security/security_connector/load_system_roots_linux.cc",
        "src/core/lib/security/security_connector/local/local_security_connector.cc",
        "src/core/lib/security/security_connector/security_connector.cc",
        "src/core/lib/security/security_connector/ssl/ssl_security_connector.cc",
        "src/core/lib/security/security_connector/ssl_utils.cc",
        "src/core/lib/security/security_connector/ssl_utils_config.cc",
        "src/core/lib/security/security_connector/tls/tls_security_connector.cc",
        "src/core/lib/security/transport/client_auth_filter.cc",
        "src/core/lib/security/transport/secure_endpoint.cc",
        "src/core/lib/security/transport/security_handshaker.cc",
        "src/core/lib/security/transport/server_auth_filter.cc",
        "src/core/lib/security/transport/tsi_error.cc",
        "src/core/lib/security/util/json_util.cc",
        "src/core/lib/surface/init_secure.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/xds/xds_channel_args.h",
        "src/core/lib/security/context/security_context.h",
        "src/core/lib/security/credentials/alts/alts_credentials.h",
        "src/core/lib/security/credentials/composite/composite_credentials.h",
        "src/core/lib/security/credentials/credentials.h",
        "src/core/lib/security/credentials/external/aws_external_account_credentials.h",
        "src/core/lib/security/credentials/external/aws_request_signer.h",
        "src/core/lib/security/credentials/external/external_account_credentials.h",
        "src/core/lib/security/credentials/external/file_external_account_credentials.h",
        "src/core/lib/security/credentials/external/url_external_account_credentials.h",
        "src/core/lib/security/credentials/fake/fake_credentials.h",
        "src/core/lib/security/credentials/google_default/google_default_credentials.h",
        "src/core/lib/security/credentials/iam/iam_credentials.h",
        "src/core/lib/security/credentials/jwt/json_token.h",
        "src/core/lib/security/credentials/jwt/jwt_credentials.h",
        "src/core/lib/security/credentials/jwt/jwt_verifier.h",
        "src/core/lib/security/credentials/local/local_credentials.h",
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.h",
        "src/core/lib/security/credentials/plugin/plugin_credentials.h",
        "src/core/lib/security/credentials/ssl/ssl_credentials.h",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h",
        "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h",
        "src/core/lib/security/credentials/tls/tls_credentials.h",
        "src/core/lib/security/credentials/tls/tls_utils.h",
        "src/core/lib/security/security_connector/alts/alts_security_connector.h",
        "src/core/lib/security/security_connector/fake/fake_security_connector.h",
        "src/core/lib/security/security_connector/insecure/insecure_security_connector.h",
        "src/core/lib/security/security_connector/load_system_roots.h",
        "src/core/lib/security/security_connector/load_system_roots_linux.h",
        "src/core/lib/security/security_connector/local/local_security_connector.h",
        "src/core/lib/security/security_connector/security_connector.h",
        "src/core/lib/security/security_connector/ssl/ssl_security_connector.h",
        "src/core/lib/security/security_connector/ssl_utils.h",
        "src/core/lib/security/security_connector/ssl_utils_config.h",
        "src/core/lib/security/security_connector/tls/tls_security_connector.h",
        "src/core/lib/security/transport/auth_filters.h",
        "src/core/lib/security/transport/secure_endpoint.h",
        "src/core/lib/security/transport/security_handshaker.h",
        "src/core/lib/security/transport/tsi_error.h",
        "src/core/lib/security/util/json_util.h",
    ],
    language = "c++",
    public_hdrs = GRPC_SECURE_PUBLIC_HDRS,
    deps = [
        "alts_util",
        "grpc_base",
        "grpc_lb_xds_channel_args",
        "grpc_transport_chttp2_alpn",
        "tsi",
    ],
)

grpc_cc_library(
    name = "grpc_mock_cel",
    hdrs = [
        "src/core/lib/security/authorization/mock_cel/activation.h",
        "src/core/lib/security/authorization/mock_cel/cel_expr_builder_factory.h",
        "src/core/lib/security/authorization/mock_cel/cel_expression.h",
        "src/core/lib/security/authorization/mock_cel/cel_value.h",
        "src/core/lib/security/authorization/mock_cel/evaluator_core.h",
        "src/core/lib/security/authorization/mock_cel/flat_expr_builder.h",
    ],
    language = "c++",
    deps = [
        "google_api_upb",
        "grpc_base",
    ],
)

# This target depends on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_matchers",
    srcs = [
        "src/core/lib/matchers/matchers.cc",
    ],
    hdrs = [
        "src/core/lib/matchers/matchers.h",
    ],
    external_deps = [
        "re2",
    ],
    language = "c++",
    deps = [
        "grpc_base",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_rbac_engine",
    srcs = [
        "src/core/lib/security/authorization/evaluate_args.cc",
        "src/core/lib/security/authorization/grpc_authorization_engine.cc",
        "src/core/lib/security/authorization/matchers.cc",
        "src/core/lib/security/authorization/rbac_policy.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/authorization_engine.h",
        "src/core/lib/security/authorization/evaluate_args.h",
        "src/core/lib/security/authorization/grpc_authorization_engine.h",
        "src/core/lib/security/authorization/matchers.h",
        "src/core/lib/security/authorization/rbac_policy.h",
    ],
    language = "c++",
    deps = [
        "grpc_base",
        "grpc_matchers",
        "grpc_secure",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_authorization_provider",
    srcs = [
        "src/core/lib/security/authorization/rbac_translator.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/rbac_translator.h",
    ],
    language = "c++",
    deps = [
        "grpc_matchers",
        "grpc_rbac_engine",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_cel_engine",
    srcs = [
        "src/core/lib/security/authorization/cel_authorization_engine.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/cel_authorization_engine.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    language = "c++",
    deps = [
        "envoy_ads_upb",
        "google_api_upb",
        "grpc_base",
        "grpc_mock_cel",
        "grpc_rbac_engine",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.cc",
        "src/core/ext/transport/chttp2/transport/bin_encoder.cc",
        "src/core/ext/transport/chttp2/transport/chttp2_plugin.cc",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.cc",
        "src/core/ext/transport/chttp2/transport/context_list.cc",
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
        "src/core/ext/transport/chttp2/transport/context_list.h",
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
    ],
    hdrs = [
        "src/core/tsi/transport_security.h",
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
    name = "alts_util",
    srcs = [
        "src/core/lib/security/credentials/alts/check_gcp_environment.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_linux.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_no_op.cc",
        "src/core/lib/security/credentials/alts/check_gcp_environment_windows.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_client_options.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.cc",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_server_options.cc",
        "src/core/tsi/alts/handshaker/transport_security_common_api.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/alts/check_gcp_environment.h",
        "src/core/lib/security/credentials/alts/grpc_alts_credentials_options.h",
        "src/core/tsi/alts/handshaker/transport_security_common_api.h",
    ],
    language = "c++",
    public_hdrs = GRPC_SECURE_PUBLIC_HDRS,
    deps = [
        "alts_upb",
        "gpr",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "tsi",
    srcs = [
        "src/core/tsi/alts/handshaker/alts_handshaker_client.cc",
        "src/core/tsi/alts/handshaker/alts_shared_resource.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.cc",
        "src/core/tsi/fake_transport_security.cc",
        "src/core/tsi/local_transport_security.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_openssl.cc",
        "src/core/tsi/ssl_transport_security.cc",
        "src/core/tsi/transport_security_grpc.cc",
    ],
    hdrs = [
        "src/core/tsi/alts/handshaker/alts_handshaker_client.h",
        "src/core/tsi/alts/handshaker/alts_shared_resource.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.h",
        "src/core/tsi/fake_transport_security.h",
        "src/core/tsi/local_transport_security.h",
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
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "grpc",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc_health_upb",
    ],
)

grpc_cc_library(
    name = "grpc++_base_unsecure",
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc_health_upb",
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
        "include/grpc++/impl/codegen/call_hook.h",
        "include/grpc++/impl/codegen/call.h",
        "include/grpc++/impl/codegen/channel_interface.h",
        "include/grpc++/impl/codegen/client_context.h",
        "include/grpc++/impl/codegen/client_unary_call.h",
        "include/grpc++/impl/codegen/completion_queue_tag.h",
        "include/grpc++/impl/codegen/completion_queue.h",
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
        "include/grpc++/impl/codegen/status_code_enum.h",
        "include/grpc++/impl/codegen/status.h",
        "include/grpc++/impl/codegen/string_ref.h",
        "include/grpc++/impl/codegen/stub_options.h",
        "include/grpc++/impl/codegen/sync_stream.h",
        "include/grpc++/impl/codegen/time.h",
        "include/grpcpp/impl/codegen/async_generic_service.h",
        "include/grpcpp/impl/codegen/async_stream.h",
        "include/grpcpp/impl/codegen/async_unary_call.h",
        "include/grpcpp/impl/codegen/byte_buffer.h",
        "include/grpcpp/impl/codegen/call_hook.h",
        "include/grpcpp/impl/codegen/call_op_set_interface.h",
        "include/grpcpp/impl/codegen/call_op_set.h",
        "include/grpcpp/impl/codegen/call.h",
        "include/grpcpp/impl/codegen/callback_common.h",
        "include/grpcpp/impl/codegen/channel_interface.h",
        "include/grpcpp/impl/codegen/client_callback.h",
        "include/grpcpp/impl/codegen/client_context.h",
        "include/grpcpp/impl/codegen/client_interceptor.h",
        "include/grpcpp/impl/codegen/client_unary_call.h",
        "include/grpcpp/impl/codegen/completion_queue_tag.h",
        "include/grpcpp/impl/codegen/completion_queue.h",
        "include/grpcpp/impl/codegen/config.h",
        "include/grpcpp/impl/codegen/core_codegen_interface.h",
        "include/grpcpp/impl/codegen/create_auth_context.h",
        "include/grpcpp/impl/codegen/delegating_channel.h",
        "include/grpcpp/impl/codegen/grpc_library.h",
        "include/grpcpp/impl/codegen/intercepted_channel.h",
        "include/grpcpp/impl/codegen/interceptor_common.h",
        "include/grpcpp/impl/codegen/interceptor.h",
        "include/grpcpp/impl/codegen/message_allocator.h",
        "include/grpcpp/impl/codegen/metadata_map.h",
        "include/grpcpp/impl/codegen/method_handler_impl.h",
        "include/grpcpp/impl/codegen/method_handler.h",
        "include/grpcpp/impl/codegen/rpc_method.h",
        "include/grpcpp/impl/codegen/rpc_service_method.h",
        "include/grpcpp/impl/codegen/security/auth_context.h",
        "include/grpcpp/impl/codegen/serialization_traits.h",
        "include/grpcpp/impl/codegen/server_callback_handlers.h",
        "include/grpcpp/impl/codegen/server_callback.h",
        "include/grpcpp/impl/codegen/server_context.h",
        "include/grpcpp/impl/codegen/server_interceptor.h",
        "include/grpcpp/impl/codegen/server_interface.h",
        "include/grpcpp/impl/codegen/service_type.h",
        "include/grpcpp/impl/codegen/slice.h",
        "include/grpcpp/impl/codegen/status_code_enum.h",
        "include/grpcpp/impl/codegen/status.h",
        "include/grpcpp/impl/codegen/string_ref.h",
        "include/grpcpp/impl/codegen/stub_options.h",
        "include/grpcpp/impl/codegen/sync_stream.h",
        "include/grpcpp/impl/codegen/time.h",
    ],
    deps = [
        "grpc++_internal_hdrs_only",
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
    external_deps = [
        "protobuf_headers",
    ],
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
    name = "grpcpp_channelz",
    srcs = [
        "src/cpp/server/channelz/channelz_service.cc",
        "src/cpp/server/channelz/channelz_service_plugin.cc",
    ],
    hdrs = [
        "src/cpp/server/channelz/channelz_service.h",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/channelz_service_plugin.h",
    ],
    deps = [
        ":grpc++",
        "//src/proto/grpc/channelz:channelz_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_csds",
    srcs = [
        "src/cpp/server/csds/csds.cc",
    ],
    hdrs = [
        "src/cpp/server/csds/csds.h",
    ],
    language = "c++",
    deps = [
        ":grpc++_internals",
        "//src/proto/grpc/testing/xds/v3:csds_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_admin",
    srcs = [
        "src/cpp/server/admin/admin_services.cc",
    ],
    hdrs = [],
    defines = select({
        "grpc_no_xds": ["GRPC_NO_XDS"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/memory",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/admin_services.h",
    ],
    select_deps = {
        "grpc_no_xds": [],
        "//conditions:default": ["//:grpcpp_csds"],
    },
    deps = [
        ":grpc++",
        ":grpcpp_channelz",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpc++_test",
    srcs = [
        "src/cpp/client/channel_test_peer.cc",
    ],
    external_deps = [
        "gtest",
    ],
    public_hdrs = [
        "include/grpc++/test/mock_stream.h",
        "include/grpc++/test/server_context_test_spouse.h",
        "include/grpcpp/test/channel_test_peer.h",
        "include/grpcpp/test/default_reactor_test_peer.h",
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

grpc_cc_library(
    name = "grpc_opencensus_plugin",
    srcs = [
        "src/cpp/ext/filters/census/channel_filter.cc",
        "src/cpp/ext/filters/census/client_filter.cc",
        "src/cpp/ext/filters/census/context.cc",
        "src/cpp/ext/filters/census/grpc_plugin.cc",
        "src/cpp/ext/filters/census/measures.cc",
        "src/cpp/ext/filters/census/rpc_encoding.cc",
        "src/cpp/ext/filters/census/server_filter.cc",
        "src/cpp/ext/filters/census/views.cc",
    ],
    hdrs = [
        "include/grpcpp/opencensus.h",
        "src/cpp/ext/filters/census/channel_filter.h",
        "src/cpp/ext/filters/census/client_filter.h",
        "src/cpp/ext/filters/census/context.h",
        "src/cpp/ext/filters/census/grpc_plugin.h",
        "src/cpp/ext/filters/census/measures.h",
        "src/cpp/ext/filters/census/rpc_encoding.h",
        "src/cpp/ext/filters/census/server_filter.h",
    ],
    external_deps = [
        "absl-base",
        "absl-time",
        "opencensus-trace",
        "opencensus-trace-context_util",
        "opencensus-trace-propagation",
        "opencensus-stats",
        "opencensus-context",
    ],
    language = "c++",
    deps = [
        ":census",
        ":grpc++",
    ],
)

# Once upb code-gen issue is resolved, use the targets commented below to replace the ones using
# upb-generated files.

# grpc_upb_proto_library(
#     name = "upb_load_report",
#     deps = ["@envoy_api//envoy/api/v2/endpoint:load_report_export"],
# )
#
# grpc_upb_proto_library(
#     name = "upb_lrs",
#     deps = ["@envoy_api//envoy/service/load_stats/v2:lrs_export"],
# )
#
# grpc_upb_proto_library(
#     name = "upb_cds",
#     deps = ["@envoy_api//envoy/api/v2:cds_export"],
# )

# grpc_cc_library(
#    name = "envoy_lrs_upb",
#    external_deps = [
#        "upb_lib",
#    ],
#    language = "c++",
#    tags = ["no_windows"],
#    deps = [
#        ":upb_load_report",
#        ":upb_lrs",
#    ],
# )

# grpc_cc_library(
#    name = "envoy_ads_upb",
#    external_deps = [
#        "upb_lib",
#    ],
#    language = "c++",
#    tags = ["no_windows"],
#    deps = [
#        ":upb_cds",
#    ],
# )

grpc_cc_library(
    name = "envoy_ads_upb",
    srcs = [
        "src/core/ext/upb-generated/envoy/admin/v3/config_dump.upb.c",
        "src/core/ext/upb-generated/envoy/config/accesslog/v3/accesslog.upb.c",
        "src/core/ext/upb-generated/envoy/config/bootstrap/v3/bootstrap.upb.c",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/circuit_breaker.upb.c",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/cluster.upb.c",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/filter.upb.c",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/outlier_detection.upb.c",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint.upb.c",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint_components.upb.c",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/load_report.upb.c",
        "src/core/ext/upb-generated/envoy/config/listener/v3/api_listener.upb.c",
        "src/core/ext/upb-generated/envoy/config/listener/v3/listener.upb.c",
        "src/core/ext/upb-generated/envoy/config/listener/v3/listener_components.upb.c",
        "src/core/ext/upb-generated/envoy/config/listener/v3/udp_listener_config.upb.c",
        "src/core/ext/upb-generated/envoy/config/metrics/v3/stats.upb.c",
        "src/core/ext/upb-generated/envoy/config/overload/v3/overload.upb.c",
        "src/core/ext/upb-generated/envoy/config/rbac/v3/rbac.upb.c",
        "src/core/ext/upb-generated/envoy/config/route/v3/route.upb.c",
        "src/core/ext/upb-generated/envoy/config/route/v3/route_components.upb.c",
        "src/core/ext/upb-generated/envoy/config/route/v3/scoped_route.upb.c",
        "src/core/ext/upb-generated/envoy/config/trace/v3/http_tracer.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/clusters/aggregate/v3/cluster.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/filters/common/fault/v3/fault.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/filters/http/fault/v3/fault.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/filters/http/router/v3/router.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/cert.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/common.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/secret.upb.c",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/tls.upb.c",
        "src/core/ext/upb-generated/envoy/service/cluster/v3/cds.upb.c",
        "src/core/ext/upb-generated/envoy/service/discovery/v3/ads.upb.c",
        "src/core/ext/upb-generated/envoy/service/discovery/v3/discovery.upb.c",
        "src/core/ext/upb-generated/envoy/service/endpoint/v3/eds.upb.c",
        "src/core/ext/upb-generated/envoy/service/listener/v3/lds.upb.c",
        "src/core/ext/upb-generated/envoy/service/load_stats/v3/lrs.upb.c",
        "src/core/ext/upb-generated/envoy/service/route/v3/rds.upb.c",
        "src/core/ext/upb-generated/envoy/service/route/v3/srds.upb.c",
        "src/core/ext/upb-generated/envoy/service/status/v3/csds.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/envoy/admin/v3/config_dump.upb.h",
        "src/core/ext/upb-generated/envoy/config/accesslog/v3/accesslog.upb.h",
        "src/core/ext/upb-generated/envoy/config/bootstrap/v3/bootstrap.upb.h",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/circuit_breaker.upb.h",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/cluster.upb.h",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/filter.upb.h",
        "src/core/ext/upb-generated/envoy/config/cluster/v3/outlier_detection.upb.h",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint.upb.h",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/endpoint_components.upb.h",
        "src/core/ext/upb-generated/envoy/config/endpoint/v3/load_report.upb.h",
        "src/core/ext/upb-generated/envoy/config/listener/v3/api_listener.upb.h",
        "src/core/ext/upb-generated/envoy/config/listener/v3/listener.upb.h",
        "src/core/ext/upb-generated/envoy/config/listener/v3/listener_components.upb.h",
        "src/core/ext/upb-generated/envoy/config/listener/v3/udp_listener_config.upb.h",
        "src/core/ext/upb-generated/envoy/config/metrics/v3/stats.upb.h",
        "src/core/ext/upb-generated/envoy/config/overload/v3/overload.upb.h",
        "src/core/ext/upb-generated/envoy/config/rbac/v3/rbac.upb.h",
        "src/core/ext/upb-generated/envoy/config/route/v3/route.upb.h",
        "src/core/ext/upb-generated/envoy/config/route/v3/route_components.upb.h",
        "src/core/ext/upb-generated/envoy/config/route/v3/scoped_route.upb.h",
        "src/core/ext/upb-generated/envoy/config/trace/v3/http_tracer.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/clusters/aggregate/v3/cluster.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/filters/common/fault/v3/fault.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/filters/http/fault/v3/fault.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/filters/http/router/v3/router.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/cert.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/common.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/secret.upb.h",
        "src/core/ext/upb-generated/envoy/extensions/transport_sockets/tls/v3/tls.upb.h",
        "src/core/ext/upb-generated/envoy/service/cluster/v3/cds.upb.h",
        "src/core/ext/upb-generated/envoy/service/discovery/v3/ads.upb.h",
        "src/core/ext/upb-generated/envoy/service/discovery/v3/discovery.upb.h",
        "src/core/ext/upb-generated/envoy/service/endpoint/v3/eds.upb.h",
        "src/core/ext/upb-generated/envoy/service/listener/v3/lds.upb.h",
        "src/core/ext/upb-generated/envoy/service/load_stats/v3/lrs.upb.h",
        "src/core/ext/upb-generated/envoy/service/route/v3/rds.upb.h",
        "src/core/ext/upb-generated/envoy/service/route/v3/srds.upb.h",
        "src/core/ext/upb-generated/envoy/service/status/v3/csds.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":envoy_annotations_upb",
        ":envoy_core_upb",
        ":envoy_type_upb",
        ":google_api_upb",
        ":proto_gen_validate_upb",
        ":udpa_annotations_upb",
        ":xds_core_upb",
    ],
)

grpc_cc_library(
    name = "envoy_ads_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/envoy/admin/v3/config_dump.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/accesslog/v3/accesslog.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/bootstrap/v3/bootstrap.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/circuit_breaker.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/cluster.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/filter.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/outlier_detection.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint_components.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/load_report.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/api_listener.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener_components.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/udp_listener_config.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/metrics/v3/stats.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/overload/v3/overload.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/route.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/route_components.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/scoped_route.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/trace/v3/http_tracer.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/common/fault/v3/fault.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/http/fault/v3/fault.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/http/router/v3/router.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/cert.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/common.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/secret.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/cluster/v3/cds.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/discovery/v3/ads.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/discovery/v3/discovery.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/endpoint/v3/eds.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/listener/v3/lds.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/load_stats/v3/lrs.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/route/v3/rds.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/route/v3/srds.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/service/status/v3/csds.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/envoy/admin/v3/config_dump.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/accesslog/v3/accesslog.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/bootstrap/v3/bootstrap.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/circuit_breaker.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/cluster.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/filter.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/cluster/v3/outlier_detection.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/endpoint_components.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/endpoint/v3/load_report.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/api_listener.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/listener_components.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/listener/v3/udp_listener_config.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/metrics/v3/stats.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/overload/v3/overload.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/route.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/route_components.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/route/v3/scoped_route.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/trace/v3/http_tracer.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/common/fault/v3/fault.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/http/fault/v3/fault.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/http/router/v3/router.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/cert.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/common.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/secret.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/cluster/v3/cds.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/discovery/v3/ads.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/discovery/v3/discovery.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/endpoint/v3/eds.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/listener/v3/lds.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/load_stats/v3/lrs.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/route/v3/rds.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/route/v3/srds.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/service/status/v3/csds.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":envoy_ads_upb",
        ":envoy_annotations_upbdefs",
        ":envoy_core_upbdefs",
        ":envoy_type_upbdefs",
        ":google_api_upbdefs",
        ":proto_gen_validate_upbdefs",
        ":udpa_annotations_upbdefs",
        ":xds_core_upbdefs",
    ],
)

grpc_cc_library(
    name = "envoy_annotations_upb",
    srcs = [
        "src/core/ext/upb-generated/envoy/annotations/deprecation.upb.c",
        "src/core/ext/upb-generated/envoy/annotations/resource.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/envoy/annotations/deprecation.upb.h",
        "src/core/ext/upb-generated/envoy/annotations/resource.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
    ],
)

grpc_cc_library(
    name = "envoy_annotations_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/envoy/annotations/deprecation.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/annotations/resource.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/envoy/annotations/deprecation.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/annotations/resource.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":envoy_annotations_upb",
        ":google_api_upbdefs",
    ],
)

grpc_cc_library(
    name = "envoy_core_upb",
    srcs = [
        "src/core/ext/upb-generated/envoy/config/core/v3/address.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/backoff.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/base.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/config_source.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/event_service_config.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/extension.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/grpc_service.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/health_check.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/http_uri.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/protocol.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/proxy_protocol.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/socket_option.upb.c",
        "src/core/ext/upb-generated/envoy/config/core/v3/substitution_format_string.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/envoy/config/core/v3/address.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/backoff.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/base.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/config_source.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/event_service_config.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/extension.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/grpc_service.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/health_check.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/http_uri.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/protocol.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/proxy_protocol.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/socket_option.upb.h",
        "src/core/ext/upb-generated/envoy/config/core/v3/substitution_format_string.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":envoy_annotations_upb",
        ":envoy_type_upb",
        ":google_api_upb",
        ":proto_gen_validate_upb",
        ":udpa_annotations_upb",
        ":xds_core_upb",
    ],
)

grpc_cc_library(
    name = "envoy_core_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/address.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/backoff.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/base.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/config_source.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/event_service_config.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/extension.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/grpc_service.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/health_check.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/http_uri.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/protocol.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/proxy_protocol.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/socket_option.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/substitution_format_string.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/address.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/backoff.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/base.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/config_source.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/event_service_config.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/extension.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/grpc_service.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/health_check.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/http_uri.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/protocol.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/proxy_protocol.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/socket_option.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/config/core/v3/substitution_format_string.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":envoy_core_upb",
        ":envoy_type_upbdefs",
        ":google_api_upbdefs",
        ":proto_gen_validate_upbdefs",
    ],
)

grpc_cc_library(
    name = "envoy_type_upb",
    srcs = [
        "src/core/ext/upb-generated/envoy/type/matcher/v3/metadata.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/node.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/number.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/path.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/regex.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/string.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/struct.upb.c",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/value.upb.c",
        "src/core/ext/upb-generated/envoy/type/metadata/v3/metadata.upb.c",
        "src/core/ext/upb-generated/envoy/type/tracing/v3/custom_tag.upb.c",
        "src/core/ext/upb-generated/envoy/type/v3/http.upb.c",
        "src/core/ext/upb-generated/envoy/type/v3/percent.upb.c",
        "src/core/ext/upb-generated/envoy/type/v3/range.upb.c",
        "src/core/ext/upb-generated/envoy/type/v3/semantic_version.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/envoy/type/matcher/v3/metadata.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/node.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/number.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/path.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/regex.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/string.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/struct.upb.h",
        "src/core/ext/upb-generated/envoy/type/matcher/v3/value.upb.h",
        "src/core/ext/upb-generated/envoy/type/metadata/v3/metadata.upb.h",
        "src/core/ext/upb-generated/envoy/type/tracing/v3/custom_tag.upb.h",
        "src/core/ext/upb-generated/envoy/type/v3/http.upb.h",
        "src/core/ext/upb-generated/envoy/type/v3/percent.upb.h",
        "src/core/ext/upb-generated/envoy/type/v3/range.upb.h",
        "src/core/ext/upb-generated/envoy/type/v3/semantic_version.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":envoy_annotations_upb",
        ":google_api_upb",
        ":proto_gen_validate_upb",
        ":udpa_annotations_upb",
    ],
)

grpc_cc_library(
    name = "envoy_type_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/metadata.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/node.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/number.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/path.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/regex.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/string.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/struct.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/value.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/metadata/v3/metadata.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/tracing/v3/custom_tag.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/v3/http.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/v3/percent.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/v3/range.upbdefs.c",
        "src/core/ext/upbdefs-generated/envoy/type/v3/semantic_version.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/metadata.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/node.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/number.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/path.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/regex.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/string.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/struct.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/matcher/v3/value.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/metadata/v3/metadata.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/tracing/v3/custom_tag.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/v3/http.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/v3/percent.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/v3/range.upbdefs.h",
        "src/core/ext/upbdefs-generated/envoy/type/v3/semantic_version.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":envoy_type_upb",
        ":google_api_upbdefs",
        ":proto_gen_validate_upbdefs",
    ],
)

grpc_cc_library(
    name = "proto_gen_validate_upb",
    srcs = [
        "src/core/ext/upb-generated/validate/validate.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/validate/validate.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
    ],
)

grpc_cc_library(
    name = "proto_gen_validate_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/validate/validate.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/validate/validate.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":google_api_upbdefs",
        ":proto_gen_validate_upb",
    ],
)

# Once upb code-gen issue is resolved, replace udpa_orca_upb with this.
# grpc_upb_proto_library(
#     name = "udpa_orca_upb",
#     deps = ["@envoy_api//udpa/data/orca/v1:orca_load_report"]
# )

grpc_cc_library(
    name = "udpa_orca_upb",
    srcs = [
        "src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/udpa/data/orca/v1/orca_load_report.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":proto_gen_validate_upb",
    ],
)

grpc_cc_library(
    name = "udpa_annotations_upb",
    srcs = [
        "src/core/ext/upb-generated/udpa/annotations/migrate.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/security.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/sensitive.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/status.upb.c",
        "src/core/ext/upb-generated/udpa/annotations/versioning.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/udpa/annotations/migrate.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/security.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/sensitive.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/status.upb.h",
        "src/core/ext/upb-generated/udpa/annotations/versioning.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
        ":proto_gen_validate_upb",
    ],
)

grpc_cc_library(
    name = "udpa_annotations_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/udpa/annotations/migrate.upbdefs.c",
        "src/core/ext/upbdefs-generated/udpa/annotations/security.upbdefs.c",
        "src/core/ext/upbdefs-generated/udpa/annotations/sensitive.upbdefs.c",
        "src/core/ext/upbdefs-generated/udpa/annotations/status.upbdefs.c",
        "src/core/ext/upbdefs-generated/udpa/annotations/versioning.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/udpa/annotations/migrate.upbdefs.h",
        "src/core/ext/upbdefs-generated/udpa/annotations/security.upbdefs.h",
        "src/core/ext/upbdefs-generated/udpa/annotations/sensitive.upbdefs.h",
        "src/core/ext/upbdefs-generated/udpa/annotations/status.upbdefs.h",
        "src/core/ext/upbdefs-generated/udpa/annotations/versioning.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":google_api_upbdefs",
        ":udpa_annotations_upb",
    ],
)

grpc_cc_library(
    name = "xds_core_upb",
    srcs = [
        "src/core/ext/upb-generated/xds/core/v3/authority.upb.c",
        "src/core/ext/upb-generated/xds/core/v3/collection_entry.upb.c",
        "src/core/ext/upb-generated/xds/core/v3/context_params.upb.c",
        "src/core/ext/upb-generated/xds/core/v3/resource.upb.c",
        "src/core/ext/upb-generated/xds/core/v3/resource_locator.upb.c",
        "src/core/ext/upb-generated/xds/core/v3/resource_name.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/xds/core/v3/authority.upb.h",
        "src/core/ext/upb-generated/xds/core/v3/collection_entry.upb.h",
        "src/core/ext/upb-generated/xds/core/v3/context_params.upb.h",
        "src/core/ext/upb-generated/xds/core/v3/resource.upb.h",
        "src/core/ext/upb-generated/xds/core/v3/resource_locator.upb.h",
        "src/core/ext/upb-generated/xds/core/v3/resource_name.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
        ":proto_gen_validate_upb",
        ":udpa_annotations_upb",
    ],
)

grpc_cc_library(
    name = "xds_core_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/xds/core/v3/authority.upbdefs.c",
        "src/core/ext/upbdefs-generated/xds/core/v3/collection_entry.upbdefs.c",
        "src/core/ext/upbdefs-generated/xds/core/v3/context_params.upbdefs.c",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource.upbdefs.c",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource_locator.upbdefs.c",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource_name.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/xds/core/v3/authority.upbdefs.h",
        "src/core/ext/upbdefs-generated/xds/core/v3/collection_entry.upbdefs.h",
        "src/core/ext/upbdefs-generated/xds/core/v3/context_params.upbdefs.h",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource.upbdefs.h",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource_locator.upbdefs.h",
        "src/core/ext/upbdefs-generated/xds/core/v3/resource_name.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":google_api_upbdefs",
        ":proto_gen_validate_upbdefs",
        ":udpa_annotations_upbdefs",
        ":xds_core_upb",
    ],
)

grpc_cc_library(
    name = "udpa_type_upb",
    srcs = [
        "src/core/ext/upb-generated/udpa/type/v1/typed_struct.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/udpa/type/v1/typed_struct.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
        ":proto_gen_validate_upb",
    ],
)

grpc_cc_library(
    name = "udpa_type_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/udpa/type/v1/typed_struct.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/udpa/type/v1/typed_struct.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":google_api_upbdefs",
        ":proto_gen_validate_upbdefs",
    ],
)

# Once upb code-gen issue is resolved, replace grpc_health_upb with this.
# grpc_upb_proto_library(
#     name = "grpc_health_upb",
#     deps = ["//src/proto/grpc/health/v1:health_proto_descriptor"],
# )

grpc_cc_library(
    name = "grpc_health_upb",
    srcs = [
        "src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/src/proto/grpc/health/v1/health.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
)

# Once upb code-gen issue is resolved, remove this.
grpc_cc_library(
    name = "google_api_upb",
    srcs = [
        "src/core/ext/upb-generated/google/api/annotations.upb.c",
        "src/core/ext/upb-generated/google/api/expr/v1alpha1/checked.upb.c",
        "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.c",
        "src/core/ext/upb-generated/google/api/http.upb.c",
        "src/core/ext/upb-generated/google/protobuf/any.upb.c",
        "src/core/ext/upb-generated/google/protobuf/duration.upb.c",
        "src/core/ext/upb-generated/google/protobuf/empty.upb.c",
        "src/core/ext/upb-generated/google/protobuf/struct.upb.c",
        "src/core/ext/upb-generated/google/protobuf/timestamp.upb.c",
        "src/core/ext/upb-generated/google/protobuf/wrappers.upb.c",
        "src/core/ext/upb-generated/google/rpc/status.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/google/api/annotations.upb.h",
        "src/core/ext/upb-generated/google/api/expr/v1alpha1/checked.upb.h",
        "src/core/ext/upb-generated/google/api/expr/v1alpha1/syntax.upb.h",
        "src/core/ext/upb-generated/google/api/http.upb.h",
        "src/core/ext/upb-generated/google/protobuf/any.upb.h",
        "src/core/ext/upb-generated/google/protobuf/duration.upb.h",
        "src/core/ext/upb-generated/google/protobuf/empty.upb.h",
        "src/core/ext/upb-generated/google/protobuf/struct.upb.h",
        "src/core/ext/upb-generated/google/protobuf/timestamp.upb.h",
        "src/core/ext/upb-generated/google/protobuf/wrappers.upb.h",
        "src/core/ext/upb-generated/google/rpc/status.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "google_api_upbdefs",
    srcs = [
        "src/core/ext/upbdefs-generated/google/api/annotations.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/api/http.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/any.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/duration.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/empty.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/struct.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/timestamp.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/protobuf/wrappers.upbdefs.c",
        "src/core/ext/upbdefs-generated/google/rpc/status.upbdefs.c",
    ],
    hdrs = [
        "src/core/ext/upbdefs-generated/google/api/annotations.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/api/http.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/any.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/duration.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/empty.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/struct.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/timestamp.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/protobuf/wrappers.upbdefs.h",
        "src/core/ext/upbdefs-generated/google/rpc/status.upbdefs.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor_reflection",
        "upb_textformat_lib",
    ],
    language = "c++",
    deps = [
        ":google_api_upb",
    ],
)

# Once upb code-gen issue is resolved, replace grpc_lb_upb with this.
# grpc_upb_proto_library(
#     name = "grpc_lb_upb",
#     deps = ["//src/proto/grpc/lb/v1:load_balancer_proto_descriptor"],
# )

grpc_cc_library(
    name = "grpc_lb_upb",
    srcs = [
        "src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/src/proto/grpc/lb/v1/load_balancer.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    deps = [
        "google_api_upb",
    ],
)

# Once upb code-gen issue is resolved, replace meshca_upb with this.
# meshca_upb_proto_library(
#     name = "meshca_upb",
#     deps = ["//third_party/istio/security/proto/providers/google:meshca_proto"],
# )

grpc_cc_library(
    name = "meshca_upb",
    srcs = [
        "src/core/ext/upb-generated/third_party/istio/security/proto/providers/google/meshca.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/third_party/istio/security/proto/providers/google/meshca.upb.h",
    ],
    language = "c++",
    deps = [
        "google_api_upb",
    ],
)

# Once upb code-gen issue is resolved, replace alts_upb with this.
# grpc_upb_proto_library(
#     name = "alts_upb",
#     deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
# )

grpc_cc_library(
    name = "alts_upb",
    srcs = [
        "src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.c",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.c",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.c",
    ],
    hdrs = [
        "src/core/ext/upb-generated/src/proto/grpc/gcp/altscontext.upb.h",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/handshaker.upb.h",
        "src/core/ext/upb-generated/src/proto/grpc/gcp/transport_security_common.upb.h",
    ],
    external_deps = [
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
)

grpc_generate_one_off_targets()

filegroup(
    name = "root_certificates",
    srcs = [
        "etc/roots.pem",
    ],
    visibility = ["//visibility:public"],
)
