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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_generate_one_off_targets",
    "grpc_upb_proto_library",
    "grpc_upb_proto_reflection_library",
    "python_config_settings",
)
load("@bazel_skylib//lib:selects.bzl", "selects")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

licenses(["reciprocal"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "layering_check",
        "-parse_headers",
    ],
)

exports_files([
    "LICENSE",
    "etc/roots.pem",
])

exports_files(
    glob(["include/**"]),
    visibility = ["//:__subpackages__"],
)

config_setting(
    name = "grpc_no_ares",
    values = {"define": "grpc_no_ares=true"},
)

config_setting(
    name = "grpc_no_xds_define",
    values = {"define": "grpc_no_xds=true"},
)

config_setting(
    name = "grpc_experiments_are_final_define",
    values = {"define": "grpc_experiments_are_final=true"},
)

bool_flag(
    name = "disable_grpc_rls",
    build_setting_default = False,
)

config_setting(
    name = "grpc_no_rls_flag",
    flag_values = {":disable_grpc_rls": "true"},
)

# When gRPC is build as shared library, binder transport code might still
# get included even when user's code does not depend on it. In that case
# --define=grpc_no_binder=true can be used to disable binder transport
# related code to reduce binary size.
# For users using build system other than bazel, they can define
# GRPC_NO_BINDER to achieve the same effect.
config_setting(
    name = "grpc_no_binder_define",
    values = {"define": "grpc_no_binder=true"},
)

config_setting(
    name = "android",
    values = {"crosstool_top": "//external:android/crosstool"},
)

config_setting(
    name = "macos",
    values = {"apple_platform_type": "macos"},
)

config_setting(
    name = "ios",
    values = {"apple_platform_type": "ios"},
)

config_setting(
    name = "tvos",
    values = {"apple_platform_type": "tvos"},
)

config_setting(
    name = "watchos",
    values = {"apple_platform_type": "watchos"},
)

config_setting(
    name = "systemd",
    values = {"define": "use_systemd=true"},
)

selects.config_setting_group(
    name = "grpc_no_xds",
    match_any = [
        ":grpc_no_xds_define",
        # In addition to disabling XDS support when --define=grpc_no_xds=true is
        # specified, we also disable it on mobile platforms where it is not
        # likely to be needed and where reducing the binary size is more
        # important.
        ":android",
        ":ios",
    ],
)

selects.config_setting_group(
    name = "grpc_no_binder",
    match_any = [
        ":grpc_no_binder_define",
        # We do not need binder on ios.
        ":ios",
    ],
)

selects.config_setting_group(
    name = "grpc_no_rls",
    match_any = [
        ":grpc_no_rls_flag",
        # Disable RLS support on mobile platforms where it is not likely to be
        # needed and where reducing the binary size is more important.
        ":android",
        ":ios",
    ],
)

selects.config_setting_group(
    name = "grpc_experiments_are_final",
    match_any = [
        ":grpc_experiments_are_final_define",
        # In addition to disabling experiments when
        # --define=grpc_experiments_are_final=true is specified, we also disable
        # them on mobile platforms where runtime configuration of experiments is unlikely to be needed and where
        # reducing the binary size is more important.
        ":android",
        ":ios",
    ],
)

# Fuzzers can be built as fuzzers or as tests
config_setting(
    name = "grpc_build_fuzzers",
    values = {"define": "grpc_build_fuzzers=true"},
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
    name = "mac",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "mac_x86_64",
    values = {"cpu": "darwin_x86_64"},
)

config_setting(
    name = "mac_arm64",
    values = {"cpu": "darwin_arm64"},
)

config_setting(
    name = "use_strict_warning",
    values = {"define": "use_strict_warning=true"},
)

python_config_settings()

# This should be updated along with build_handwritten.yaml
g_stands_for = "goku"  # @unused

core_version = "34.0.0"  # @unused

version = "1.58.0-dev"  # @unused

GPR_PUBLIC_HDRS = [
    "include/grpc/support/alloc.h",
    "include/grpc/support/atm_gcc_atomic.h",
    "include/grpc/support/atm_gcc_sync.h",
    "include/grpc/support/atm_windows.h",
    "include/grpc/support/cpu.h",
    "include/grpc/support/json.h",
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
    "include/grpc/impl/codegen/atm.h",
    "include/grpc/impl/codegen/atm_gcc_atomic.h",
    "include/grpc/impl/codegen/atm_gcc_sync.h",
    "include/grpc/impl/codegen/atm_windows.h",
    "include/grpc/impl/codegen/fork.h",
    "include/grpc/impl/codegen/gpr_types.h",
    "include/grpc/impl/codegen/log.h",
    "include/grpc/impl/codegen/port_platform.h",
    "include/grpc/impl/codegen/sync.h",
    "include/grpc/impl/codegen/sync_abseil.h",
    "include/grpc/impl/codegen/sync_custom.h",
    "include/grpc/impl/codegen/sync_generic.h",
    "include/grpc/impl/codegen/sync_posix.h",
    "include/grpc/impl/codegen/sync_windows.h",
]

GRPC_PUBLIC_HDRS = [
    "include/grpc/grpc_audit_logging.h",
    "include/grpc/byte_buffer.h",
    "include/grpc/byte_buffer_reader.h",
    "include/grpc/compression.h",
    "include/grpc/fork.h",
    "include/grpc/grpc.h",
    "include/grpc/grpc_posix.h",
    "include/grpc/grpc_security.h",
    "include/grpc/grpc_security_constants.h",
    "include/grpc/slice.h",
    "include/grpc/slice_buffer.h",
    "include/grpc/status.h",
    "include/grpc/load_reporting.h",
    "include/grpc/support/workaround_list.h",
    "include/grpc/impl/codegen/byte_buffer.h",
    "include/grpc/impl/codegen/byte_buffer_reader.h",
    "include/grpc/impl/codegen/compression_types.h",
    "include/grpc/impl/codegen/connectivity_state.h",
    "include/grpc/impl/codegen/grpc_types.h",
    "include/grpc/impl/codegen/propagation_bits.h",
    "include/grpc/impl/codegen/status.h",
    "include/grpc/impl/codegen/slice.h",
    "include/grpc/impl/compression_types.h",
    "include/grpc/impl/connectivity_state.h",
    "include/grpc/impl/grpc_types.h",
    "include/grpc/impl/propagation_bits.h",
    "include/grpc/impl/slice_type.h",
]

GRPC_PUBLIC_EVENT_ENGINE_HDRS = [
    "include/grpc/event_engine/endpoint_config.h",
    "include/grpc/event_engine/event_engine.h",
    "include/grpc/event_engine/port.h",
    "include/grpc/event_engine/memory_allocator.h",
    "include/grpc/event_engine/memory_request.h",
    "include/grpc/event_engine/internal/memory_allocator_impl.h",
    "include/grpc/event_engine/slice.h",
    "include/grpc/event_engine/slice_buffer.h",
    "include/grpc/event_engine/internal/slice_cast.h",
]

GRPCXX_SRCS = [
    "src/cpp/client/channel_cc.cc",
    "src/cpp/client/client_callback.cc",
    "src/cpp/client/client_context.cc",
    "src/cpp/client/client_interceptor.cc",
    "src/cpp/client/client_stats_interceptor.cc",
    "src/cpp/client/create_channel.cc",
    "src/cpp/client/create_channel_internal.cc",
    "src/cpp/client/create_channel_posix.cc",
    "src/cpp/common/alarm.cc",
    "src/cpp/common/channel_arguments.cc",
    "src/cpp/common/channel_filter.cc",
    "src/cpp/common/completion_queue_cc.cc",
    "src/cpp/common/resource_quota_cc.cc",
    "src/cpp/common/rpc_method.cc",
    "src/cpp/common/version_cc.cc",
    "src/cpp/common/validate_service_config.cc",
    "src/cpp/server/async_generic_service.cc",
    "src/cpp/server/channel_argument_option.cc",
    "src/cpp/server/create_default_thread_pool.cc",
    "src/cpp/server/external_connection_acceptor_impl.cc",
    "src/cpp/server/health/default_health_check_service.cc",
    "src/cpp/server/health/health_check_service.cc",
    "src/cpp/server/health/health_check_service_server_builder_option.cc",
    "src/cpp/server/server_builder.cc",
    "src/cpp/server/server_callback.cc",
    "src/cpp/server/server_cc.cc",
    "src/cpp/server/server_context.cc",
    "src/cpp/server/server_posix.cc",
    "src/cpp/thread_manager/thread_manager.cc",
    "src/cpp/util/byte_buffer_cc.cc",
    "src/cpp/util/string_ref.cc",
    "src/cpp/util/time_cc.cc",
]

GRPCXX_HDRS = [
    "src/cpp/client/create_channel_internal.h",
    "src/cpp/client/client_stats_interceptor.h",
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
    "include/grpcpp/impl/call_hook.h",
    "include/grpcpp/impl/call_op_set_interface.h",
    "include/grpcpp/impl/call_op_set.h",
    "include/grpcpp/impl/call.h",
    "include/grpcpp/impl/channel_argument_option.h",
    "include/grpcpp/impl/channel_interface.h",
    "include/grpcpp/impl/client_unary_call.h",
    "include/grpcpp/impl/completion_queue_tag.h",
    "include/grpcpp/impl/create_auth_context.h",
    "include/grpcpp/impl/delegating_channel.h",
    "include/grpcpp/impl/grpc_library.h",
    "include/grpcpp/impl/intercepted_channel.h",
    "include/grpcpp/impl/interceptor_common.h",
    "include/grpcpp/impl/metadata_map.h",
    "include/grpcpp/impl/method_handler_impl.h",
    "include/grpcpp/impl/rpc_method.h",
    "include/grpcpp/impl/rpc_service_method.h",
    "include/grpcpp/impl/serialization_traits.h",
    "include/grpcpp/impl/server_builder_option.h",
    "include/grpcpp/impl/server_builder_plugin.h",
    "include/grpcpp/impl/server_callback_handlers.h",
    "include/grpcpp/impl/server_initializer.h",
    "include/grpcpp/impl/service_type.h",
    "include/grpcpp/impl/status.h",
    "include/grpcpp/impl/sync.h",
    "include/grpcpp/resource_quota.h",
    "include/grpcpp/security/audit_logging.h",
    "include/grpcpp/security/auth_context.h",
    "include/grpcpp/security/auth_metadata_processor.h",
    "include/grpcpp/security/credentials.h",
    "include/grpcpp/security/server_credentials.h",
    "include/grpcpp/security/tls_certificate_provider.h",
    "include/grpcpp/security/authorization_policy_provider.h",
    "include/grpcpp/security/tls_certificate_verifier.h",
    "include/grpcpp/security/tls_credentials_options.h",
    "include/grpcpp/server.h",
    "include/grpcpp/server_builder.h",
    "include/grpcpp/server_context.h",
    "include/grpcpp/server_interface.h",
    "include/grpcpp/server_posix.h",
    "include/grpcpp/version_info.h",
    "include/grpcpp/support/async_stream.h",
    "include/grpcpp/support/async_unary_call.h",
    "include/grpcpp/support/byte_buffer.h",
    "include/grpcpp/support/callback_common.h",
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
    "include/grpc++/impl/codegen/create_auth_context.h",
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
    "include/grpcpp/impl/codegen/create_auth_context.h",
    "include/grpcpp/impl/codegen/delegating_channel.h",
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
    "include/grpcpp/impl/codegen/sync.h",
]

grpc_cc_library(
    name = "grpc_unsecure",
    srcs = [
        "//src/core:lib/surface/init.cc",
        "//src/core:plugin_registry/grpc_plugin_registry.cc",
        "//src/core:plugin_registry/grpc_plugin_registry_noextra.cc",
    ],
    defines = ["GRPC_NO_XDS"],
    external_deps = [
        "absl/base:core_headers",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    tags = [
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_common",
        "grpc_http_filters",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
        "iomgr_timer",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:default_event_engine",
        "//src/core:experiments",
        "//src/core:forkable",
        "//src/core:grpc_authorization_base",
        "//src/core:init_internally",
        "//src/core:posix_event_engine_timer_manager",
        "//src/core:slice",
        "//src/core:tcp_connect_handshaker",
    ],
)

GRPC_XDS_TARGETS = [
    "//src/core:grpc_lb_policy_cds",
    "//src/core:grpc_lb_policy_xds_cluster_impl",
    "//src/core:grpc_lb_policy_xds_cluster_manager",
    "//src/core:grpc_lb_policy_xds_cluster_resolver",
    "//src/core:grpc_lb_policy_xds_override_host",
    "//src/core:grpc_lb_policy_xds_wrr_locality",
    "//src/core:grpc_lb_policy_ring_hash",
    "//src/core:grpc_resolver_xds",
    "//src/core:grpc_resolver_c2p",
    "//src/core:grpc_xds_server_config_fetcher",
    "//src/core:grpc_stateful_session_filter",

    # Not xDS-specific but currently only used by xDS.
    "//src/core:channel_creds_registry_init",
]

grpc_cc_library(
    name = "grpc",
    srcs = [
        "//src/core:lib/surface/init.cc",
        "//src/core:plugin_registry/grpc_plugin_registry.cc",
        "//src/core:plugin_registry/grpc_plugin_registry_extra.cc",
    ],
    defines = select({
        ":grpc_no_xds": ["GRPC_NO_XDS"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    select_deps = [
        {
            ":grpc_no_xds": [],
            "//conditions:default": GRPC_XDS_TARGETS,
        },
    ],
    tags = [
        "nofixdeps",
    ],
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "gpr",
        "grpc_alts_credentials",
        "grpc_base",
        "grpc_client_channel",
        "grpc_common",
        "grpc_credentials_util",
        "grpc_http_filters",
        "grpc_jwt_credentials",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
        "httpcli",
        "iomgr_timer",
        "promise",
        "ref_counted_ptr",
        "sockaddr_utils",
        "tsi_base",
        "uri_parser",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:default_event_engine",
        "//src/core:experiments",
        "//src/core:forkable",
        "//src/core:grpc_authorization_base",
        "//src/core:grpc_external_account_credentials",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_google_default_credentials",
        "//src/core:grpc_iam_credentials",
        "//src/core:grpc_insecure_credentials",
        "//src/core:grpc_local_credentials",
        "//src/core:grpc_oauth2_credentials",
        "//src/core:grpc_ssl_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:grpc_transport_chttp2_alpn",
        "//src/core:httpcli_ssl_credentials",
        "//src/core:init_internally",
        "//src/core:json",
        "//src/core:posix_event_engine_timer_manager",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:tcp_connect_handshaker",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "gpr",
    srcs = [
        "//src/core:lib/gpr/alloc.cc",
        "//src/core:lib/gpr/android/log.cc",
        "//src/core:lib/gpr/iphone/cpu.cc",
        "//src/core:lib/gpr/linux/cpu.cc",
        "//src/core:lib/gpr/linux/log.cc",
        "//src/core:lib/gpr/log.cc",
        "//src/core:lib/gpr/msys/tmpfile.cc",
        "//src/core:lib/gpr/posix/cpu.cc",
        "//src/core:lib/gpr/posix/log.cc",
        "//src/core:lib/gpr/posix/string.cc",
        "//src/core:lib/gpr/posix/sync.cc",
        "//src/core:lib/gpr/posix/time.cc",
        "//src/core:lib/gpr/posix/tmpfile.cc",
        "//src/core:lib/gpr/string.cc",
        "//src/core:lib/gpr/sync.cc",
        "//src/core:lib/gpr/sync_abseil.cc",
        "//src/core:lib/gpr/time.cc",
        "//src/core:lib/gpr/time_precise.cc",
        "//src/core:lib/gpr/windows/cpu.cc",
        "//src/core:lib/gpr/windows/log.cc",
        "//src/core:lib/gpr/windows/string.cc",
        "//src/core:lib/gpr/windows/string_util.cc",
        "//src/core:lib/gpr/windows/sync.cc",
        "//src/core:lib/gpr/windows/time.cc",
        "//src/core:lib/gpr/windows/tmpfile.cc",
        "//src/core:lib/gpr/wrap_memcpy.cc",
        "//src/core:lib/gprpp/crash.cc",
        "//src/core:lib/gprpp/fork.cc",
        "//src/core:lib/gprpp/host_port.cc",
        "//src/core:lib/gprpp/mpscq.cc",
        "//src/core:lib/gprpp/posix/stat.cc",
        "//src/core:lib/gprpp/posix/thd.cc",
        "//src/core:lib/gprpp/time_util.cc",
        "//src/core:lib/gprpp/windows/stat.cc",
        "//src/core:lib/gprpp/windows/thd.cc",
    ],
    hdrs = [
        "//src/core:lib/gpr/alloc.h",
        "//src/core:lib/gpr/string.h",
        "//src/core:lib/gpr/time_precise.h",
        "//src/core:lib/gpr/tmpfile.h",
        "//src/core:lib/gprpp/crash.h",
        "//src/core:lib/gprpp/fork.h",
        "//src/core:lib/gprpp/host_port.h",
        "//src/core:lib/gprpp/memory.h",
        "//src/core:lib/gprpp/mpscq.h",
        "//src/core:lib/gprpp/stat.h",
        "//src/core:lib/gprpp/sync.h",
        "//src/core:lib/gprpp/thd.h",
        "//src/core:lib/gprpp/time_util.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/time:time",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    tags = [
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "config_vars",
        "debug_location",
        "//src/core:construct_destruct",
        "//src/core:env",
        "//src/core:event_engine_thread_local",
        "//src/core:examine_stack",
        "//src/core:gpr_atm",
        "//src/core:no_destruct",
        "//src/core:strerror",
        "//src/core:tchar",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "gpr_public_hdrs",
    hdrs = GPR_PUBLIC_HDRS,
    external_deps = [
        "absl/strings",
        "absl/types:variant",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
)

grpc_cc_library(
    name = "cpp_impl_of",
    hdrs = ["//src/core:lib/gprpp/cpp_impl_of.h"],
    language = "c++",
)

grpc_cc_library(
    name = "grpc_common",
    defines = select({
        "grpc_no_rls": ["GRPC_NO_RLS"],
        "//conditions:default": [],
    }),
    language = "c++",
    select_deps = [
        {
            "grpc_no_rls": [],
            "//conditions:default": ["//src/core:grpc_lb_policy_rls"],
        },
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_base",
        # standard plugins
        "census",
        "//src/core:grpc_backend_metric_filter",
        "//src/core:grpc_deadline_filter",
        "//src/core:grpc_client_authority_filter",
        "//src/core:grpc_lb_policy_grpclb",
        "//src/core:grpc_lb_policy_outlier_detection",
        "//src/core:grpc_lb_policy_pick_first",
        "//src/core:grpc_lb_policy_priority",
        "//src/core:grpc_lb_policy_round_robin",
        "//src/core:grpc_lb_policy_weighted_round_robin",
        "//src/core:grpc_lb_policy_weighted_target",
        "//src/core:grpc_channel_idle_filter",
        "//src/core:grpc_message_size_filter",
        "//src/core:grpc_resolver_binder",
        "grpc_resolver_dns_ares",
        "grpc_resolver_fake",
        "//src/core:grpc_resolver_dns_native",
        "//src/core:grpc_resolver_sockaddr",
        "//src/core:grpc_transport_chttp2_client_connector",
        "//src/core:grpc_transport_chttp2_server",
        "//src/core:grpc_transport_inproc",
        "//src/core:grpc_fault_injection_filter",
        "//src/core:grpc_resolver_dns_plugin",
    ],
)

grpc_cc_library(
    name = "grpc_public_hdrs",
    hdrs = GRPC_PUBLIC_HDRS,
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    deps = [
        "channel_arg_names",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
    external_deps = [
        "absl/strings:cord",
        "absl/synchronization",
        "protobuf_headers",
        "protobuf",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "grpc_public_hdrs",
        "//src/core:gpr_atm",
    ],
)

grpc_cc_library(
    name = "channel_arg_names",
    hdrs = ["include/grpc/impl/channel_arg_names.h"],
)

grpc_cc_library(
    name = "grpc++",
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    external_deps = [
        "absl/strings:cord",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    select_deps = [
        {
            ":grpc_no_xds": [],
            "//conditions:default": [
                "grpc++_xds_client",
                "grpc++_xds_server",
            ],
        },
        {
            "grpc_no_binder": [],
            "//conditions:default": [
                "grpc++_binder",
            ],
        },
    ],
    tags = ["nofixdeps"],
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "grpc++_base",
        "//src/core:gpr_atm",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "grpc_cronet_hdrs",
    hdrs = [
        "include/grpc/grpc_cronet.h",
    ],
    deps = [
        "gpr_public_hdrs",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc++_cronet_credentials",
    srcs = [
        "src/cpp/client/cronet_credentials.cc",
    ],
    hdrs = [
        "include/grpcpp/security/cronet_credentials.h",
    ],
    language = "c++",
    deps = [
        "grpc++_base",
        "grpc_cronet_hdrs",
        "grpc_public_hdrs",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_authorization_provider",
    srcs = [
        "//src/core:lib/security/authorization/grpc_authorization_policy_provider.cc",
        "//src/core:lib/security/authorization/rbac_translator.cc",
    ],
    hdrs = [
        "//src/core:lib/security/authorization/grpc_authorization_policy_provider.h",
        "//src/core:lib/security/authorization/rbac_translator.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "ref_counted_ptr",
        "//src/core:error",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_authorization_base",
        "//src/core:grpc_matchers",
        "//src/core:grpc_rbac_engine",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:useful",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc++_authorization_provider",
    srcs = [
        "src/cpp/server/authorization_policy_provider.cc",
    ],
    hdrs = [
        "include/grpcpp/security/authorization_policy_provider.h",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "grpc++",
        "grpc++_public_hdrs",
        "grpc_authorization_provider",
        "grpc_public_hdrs",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_cel_engine",
    srcs = [
        "//src/core:lib/security/authorization/cel_authorization_engine.cc",
    ],
    hdrs = [
        "//src/core:lib/security/authorization/cel_authorization_engine.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/strings",
        "absl/types:optional",
        "absl/types:span",
        "upb_collections_lib",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "envoy_config_rbac_upb",
        "google_type_expr_upb",
        "gpr",
        "grpc_mock_cel",
        "//src/core:grpc_authorization_base",
    ],
)

grpc_cc_library(
    name = "grpc++_binder",
    srcs = [
        "//src/core:ext/transport/binder/client/binder_connector.cc",
        "//src/core:ext/transport/binder/client/channel_create.cc",
        "//src/core:ext/transport/binder/client/channel_create_impl.cc",
        "//src/core:ext/transport/binder/client/connection_id_generator.cc",
        "//src/core:ext/transport/binder/client/endpoint_binder_pool.cc",
        "//src/core:ext/transport/binder/client/jni_utils.cc",
        "//src/core:ext/transport/binder/client/security_policy_setting.cc",
        "//src/core:ext/transport/binder/security_policy/binder_security_policy.cc",
        "//src/core:ext/transport/binder/server/binder_server.cc",
        "//src/core:ext/transport/binder/server/binder_server_credentials.cc",
        "//src/core:ext/transport/binder/transport/binder_transport.cc",
        "//src/core:ext/transport/binder/utils/ndk_binder.cc",
        "//src/core:ext/transport/binder/utils/transport_stream_receiver_impl.cc",
        "//src/core:ext/transport/binder/wire_format/binder_android.cc",
        "//src/core:ext/transport/binder/wire_format/binder_constants.cc",
        "//src/core:ext/transport/binder/wire_format/transaction.cc",
        "//src/core:ext/transport/binder/wire_format/wire_reader_impl.cc",
        "//src/core:ext/transport/binder/wire_format/wire_writer.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/binder/client/binder_connector.h",
        "//src/core:ext/transport/binder/client/channel_create_impl.h",
        "//src/core:ext/transport/binder/client/connection_id_generator.h",
        "//src/core:ext/transport/binder/client/endpoint_binder_pool.h",
        "//src/core:ext/transport/binder/client/jni_utils.h",
        "//src/core:ext/transport/binder/client/security_policy_setting.h",
        "//src/core:ext/transport/binder/server/binder_server.h",
        "//src/core:ext/transport/binder/transport/binder_stream.h",
        "//src/core:ext/transport/binder/transport/binder_transport.h",
        "//src/core:ext/transport/binder/utils/binder_auto_utils.h",
        "//src/core:ext/transport/binder/utils/ndk_binder.h",
        "//src/core:ext/transport/binder/utils/transport_stream_receiver.h",
        "//src/core:ext/transport/binder/utils/transport_stream_receiver_impl.h",
        "//src/core:ext/transport/binder/wire_format/binder.h",
        "//src/core:ext/transport/binder/wire_format/binder_android.h",
        "//src/core:ext/transport/binder/wire_format/binder_constants.h",
        "//src/core:ext/transport/binder/wire_format/transaction.h",
        "//src/core:ext/transport/binder/wire_format/wire_reader.h",
        "//src/core:ext/transport/binder/wire_format/wire_reader_impl.h",
        "//src/core:ext/transport/binder/wire_format/wire_writer.h",
    ],
    defines = select({
        "grpc_no_binder": ["GRPC_NO_BINDER"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/memory",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
        "absl/time",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/security/binder_security_policy.h",
        "include/grpcpp/create_channel_binder.h",
        "include/grpcpp/security/binder_credentials.h",
    ],
    tags = ["nofixdeps"],
    deps = [
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "gpr_platform",
        "grpc",
        "grpc++_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_public_hdrs",
        "orphanable",
        "ref_counted_ptr",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_stack_type",
        "//src/core:default_event_engine",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:transport_fwd",
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
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "exec_ctx",
        "gpr",
        "grpc",
        "grpc++_base",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
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
    visibility = ["@grpc:xds"],
    deps = [
        "gpr",
        "grpc",
        "grpc++_base",
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
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc++_base_unsecure",
        "grpc++_codegen_proto",
        "grpc_public_hdrs",
        "grpc_unsecure",
        "//src/core:grpc_insecure_credentials",
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
    visibility = ["@grpc:public"],
    deps = ["grpc++"],
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
    external_deps = [
        "upb_collections_lib",
        "upb_lib",
    ],
    language = "c++",
    standalone = True,
    visibility = ["@grpc:public"],
    deps = [
        "alts_upb",
        "gpr",
        "grpc++",
        "grpc_base",
        "tsi_alts_credentials",
    ],
)

grpc_cc_library(
    name = "census",
    srcs = [
        "//src/core:ext/filters/census/grpc_context.cc",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc/census.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "legacy_context",
    ],
)

# A library that vends only port_platform, so that libraries that don't need
# anything else from gpr can still be portable!
grpc_cc_library(
    name = "gpr_platform",
    language = "c++",
    public_hdrs = [
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/support/port_platform.h",
    ],
)

grpc_cc_library(
    name = "event_engine_base_hdrs",
    hdrs = GRPC_PUBLIC_EVENT_ENGINE_HDRS + GRPC_PUBLIC_HDRS,
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/time",
        "absl/types:optional",
        "absl/functional:any_invocable",
    ],
    tags = [
        "nofixdeps",
    ],
    deps = [
        "channel_arg_names",
        "gpr",
    ],
)

grpc_cc_library(
    name = "grpc_base",
    srcs = [
        "//src/core:lib/channel/call_tracer.cc",
        "//src/core:lib/channel/channel_stack.cc",
        "//src/core:lib/channel/channel_stack_builder_impl.cc",
        "//src/core:lib/channel/channel_trace.cc",
        "//src/core:lib/channel/channelz.cc",
        "//src/core:lib/channel/channelz_registry.cc",
        "//src/core:lib/channel/connected_channel.cc",
        "//src/core:lib/channel/promise_based_filter.cc",
        "//src/core:lib/channel/server_call_tracer_filter.cc",
        "//src/core:lib/channel/status_util.cc",
        "//src/core:lib/compression/compression.cc",
        "//src/core:lib/compression/compression_internal.cc",
        "//src/core:lib/compression/message_compress.cc",
        "//src/core:lib/iomgr/call_combiner.cc",
        "//src/core:lib/iomgr/cfstream_handle.cc",
        "//src/core:lib/iomgr/dualstack_socket_posix.cc",
        "//src/core:lib/iomgr/endpoint.cc",
        "//src/core:lib/iomgr/endpoint_cfstream.cc",
        "//src/core:lib/iomgr/endpoint_pair_posix.cc",
        "//src/core:lib/iomgr/endpoint_pair_windows.cc",
        "//src/core:lib/iomgr/error_cfstream.cc",
        "//src/core:lib/iomgr/ev_apple.cc",
        "//src/core:lib/iomgr/ev_epoll1_linux.cc",
        "//src/core:lib/iomgr/ev_poll_posix.cc",
        "//src/core:lib/iomgr/ev_posix.cc",
        "//src/core:lib/iomgr/ev_windows.cc",
        "//src/core:lib/iomgr/fork_posix.cc",
        "//src/core:lib/iomgr/fork_windows.cc",
        "//src/core:lib/iomgr/gethostname_fallback.cc",
        "//src/core:lib/iomgr/gethostname_host_name_max.cc",
        "//src/core:lib/iomgr/gethostname_sysconf.cc",
        "//src/core:lib/iomgr/iocp_windows.cc",
        "//src/core:lib/iomgr/iomgr.cc",
        "//src/core:lib/iomgr/iomgr_posix.cc",
        "//src/core:lib/iomgr/iomgr_posix_cfstream.cc",
        "//src/core:lib/iomgr/iomgr_windows.cc",
        "//src/core:lib/iomgr/load_file.cc",
        "//src/core:lib/iomgr/lockfree_event.cc",
        "//src/core:lib/iomgr/polling_entity.cc",
        "//src/core:lib/iomgr/pollset.cc",
        "//src/core:lib/iomgr/pollset_set_windows.cc",
        "//src/core:lib/iomgr/pollset_windows.cc",
        "//src/core:lib/iomgr/resolve_address.cc",
        "//src/core:lib/iomgr/resolve_address_posix.cc",
        "//src/core:lib/iomgr/resolve_address_windows.cc",
        "//src/core:lib/iomgr/socket_factory_posix.cc",
        "//src/core:lib/iomgr/socket_utils_common_posix.cc",
        "//src/core:lib/iomgr/socket_utils_linux.cc",
        "//src/core:lib/iomgr/socket_utils_posix.cc",
        "//src/core:lib/iomgr/socket_windows.cc",
        "//src/core:lib/iomgr/systemd_utils.cc",
        "//src/core:lib/iomgr/tcp_client.cc",
        "//src/core:lib/iomgr/tcp_client_cfstream.cc",
        "//src/core:lib/iomgr/tcp_client_posix.cc",
        "//src/core:lib/iomgr/tcp_client_windows.cc",
        "//src/core:lib/iomgr/tcp_posix.cc",
        "//src/core:lib/iomgr/tcp_server.cc",
        "//src/core:lib/iomgr/tcp_server_posix.cc",
        "//src/core:lib/iomgr/tcp_server_utils_posix_common.cc",
        "//src/core:lib/iomgr/tcp_server_utils_posix_ifaddrs.cc",
        "//src/core:lib/iomgr/tcp_server_utils_posix_noifaddrs.cc",
        "//src/core:lib/iomgr/tcp_server_windows.cc",
        "//src/core:lib/iomgr/tcp_windows.cc",
        "//src/core:lib/iomgr/unix_sockets_posix.cc",
        "//src/core:lib/iomgr/unix_sockets_posix_noop.cc",
        "//src/core:lib/iomgr/vsock.cc",
        "//src/core:lib/iomgr/wakeup_fd_eventfd.cc",
        "//src/core:lib/iomgr/wakeup_fd_nospecial.cc",
        "//src/core:lib/iomgr/wakeup_fd_pipe.cc",
        "//src/core:lib/iomgr/wakeup_fd_posix.cc",
        "//src/core:lib/resource_quota/api.cc",
        "//src/core:lib/slice/b64.cc",
        "//src/core:lib/surface/api_trace.cc",
        "//src/core:lib/surface/builtins.cc",
        "//src/core:lib/surface/byte_buffer.cc",
        "//src/core:lib/surface/byte_buffer_reader.cc",
        "//src/core:lib/surface/call.cc",
        "//src/core:lib/surface/call_details.cc",
        "//src/core:lib/surface/call_log_batch.cc",
        "//src/core:lib/surface/call_trace.cc",
        "//src/core:lib/surface/channel.cc",
        "//src/core:lib/surface/channel_ping.cc",
        "//src/core:lib/surface/completion_queue.cc",
        "//src/core:lib/surface/completion_queue_factory.cc",
        "//src/core:lib/surface/event_string.cc",
        "//src/core:lib/surface/lame_client.cc",
        "//src/core:lib/surface/metadata_array.cc",
        "//src/core:lib/surface/server.cc",
        "//src/core:lib/surface/validate_metadata.cc",
        "//src/core:lib/surface/version.cc",
        "//src/core:lib/transport/batch_builder.cc",
        "//src/core:lib/transport/connectivity_state.cc",
        "//src/core:lib/transport/error_utils.cc",
        "//src/core:lib/transport/metadata_batch.cc",
        "//src/core:lib/transport/parsed_metadata.cc",
        "//src/core:lib/transport/status_conversion.cc",
        "//src/core:lib/transport/timeout_encoding.cc",
        "//src/core:lib/transport/transport.cc",
        "//src/core:lib/transport/transport_op_string.cc",
    ] +
    # TODO(vigneshbabu): remove these
    # These headers used to be vended by this target, but they have to be
    # removed after landing EventEngine.
    [
        "//src/core:lib/iomgr/event_engine_shims/closure.cc",
        "//src/core:lib/iomgr/event_engine_shims/endpoint.cc",
        "//src/core:lib/iomgr/event_engine_shims/tcp_client.cc",
    ],
    hdrs = [
        "//src/core:lib/channel/call_finalization.h",
        "//src/core:lib/channel/call_tracer.h",
        "//src/core:lib/channel/channel_stack.h",
        "//src/core:lib/channel/channel_stack_builder_impl.h",
        "//src/core:lib/channel/channel_trace.h",
        "//src/core:lib/channel/channelz.h",
        "//src/core:lib/channel/channelz_registry.h",
        "//src/core:lib/channel/connected_channel.h",
        "//src/core:lib/channel/promise_based_filter.h",
        "//src/core:lib/channel/status_util.h",
        "//src/core:lib/compression/compression_internal.h",
        "//src/core:lib/compression/message_compress.h",
        "//src/core:lib/iomgr/block_annotate.h",
        "//src/core:lib/iomgr/call_combiner.h",
        "//src/core:lib/iomgr/cfstream_handle.h",
        "//src/core:lib/iomgr/dynamic_annotations.h",
        "//src/core:lib/iomgr/endpoint.h",
        "//src/core:lib/iomgr/endpoint_cfstream.h",
        "//src/core:lib/iomgr/endpoint_pair.h",
        "//src/core:lib/iomgr/error_cfstream.h",
        "//src/core:lib/iomgr/ev_apple.h",
        "//src/core:lib/iomgr/ev_epoll1_linux.h",
        "//src/core:lib/iomgr/ev_poll_posix.h",
        "//src/core:lib/iomgr/ev_posix.h",
        "//src/core:lib/iomgr/gethostname.h",
        "//src/core:lib/iomgr/iocp_windows.h",
        "//src/core:lib/iomgr/iomgr.h",
        "//src/core:lib/iomgr/load_file.h",
        "//src/core:lib/iomgr/lockfree_event.h",
        "//src/core:lib/iomgr/nameser.h",
        "//src/core:lib/iomgr/polling_entity.h",
        "//src/core:lib/iomgr/pollset.h",
        "//src/core:lib/iomgr/pollset_set_windows.h",
        "//src/core:lib/iomgr/pollset_windows.h",
        "//src/core:lib/iomgr/python_util.h",
        "//src/core:lib/iomgr/resolve_address.h",
        "//src/core:lib/iomgr/resolve_address_impl.h",
        "//src/core:lib/iomgr/resolve_address_posix.h",
        "//src/core:lib/iomgr/resolve_address_windows.h",
        "//src/core:lib/iomgr/sockaddr.h",
        "//src/core:lib/iomgr/sockaddr_posix.h",
        "//src/core:lib/iomgr/sockaddr_windows.h",
        "//src/core:lib/iomgr/socket_factory_posix.h",
        "//src/core:lib/iomgr/socket_utils_posix.h",
        "//src/core:lib/iomgr/socket_windows.h",
        "//src/core:lib/iomgr/systemd_utils.h",
        "//src/core:lib/iomgr/tcp_client.h",
        "//src/core:lib/iomgr/tcp_client_posix.h",
        "//src/core:lib/iomgr/tcp_posix.h",
        "//src/core:lib/iomgr/tcp_server.h",
        "//src/core:lib/iomgr/tcp_server_utils_posix.h",
        "//src/core:lib/iomgr/tcp_windows.h",
        "//src/core:lib/iomgr/unix_sockets_posix.h",
        "//src/core:lib/iomgr/vsock.h",
        "//src/core:lib/iomgr/wakeup_fd_pipe.h",
        "//src/core:lib/iomgr/wakeup_fd_posix.h",
        "//src/core:lib/resource_quota/api.h",
        "//src/core:lib/slice/b64.h",
        "//src/core:lib/surface/api_trace.h",
        "//src/core:lib/surface/builtins.h",
        "//src/core:lib/surface/call.h",
        "//src/core:lib/surface/call_test_only.h",
        "//src/core:lib/surface/call_trace.h",
        "//src/core:lib/surface/channel.h",
        "//src/core:lib/surface/completion_queue.h",
        "//src/core:lib/surface/completion_queue_factory.h",
        "//src/core:lib/surface/event_string.h",
        "//src/core:lib/surface/init.h",
        "//src/core:lib/surface/lame_client.h",
        "//src/core:lib/surface/server.h",
        "//src/core:lib/surface/validate_metadata.h",
        "//src/core:lib/transport/batch_builder.h",
        "//src/core:lib/transport/connectivity_state.h",
        "//src/core:lib/transport/custom_metadata.h",
        "//src/core:lib/transport/error_utils.h",
        "//src/core:lib/transport/metadata_batch.h",
        "//src/core:lib/transport/parsed_metadata.h",
        "//src/core:lib/transport/simple_slice_based_metadata.h",
        "//src/core:lib/transport/status_conversion.h",
        "//src/core:lib/transport/timeout_encoding.h",
        "//src/core:lib/transport/transport.h",
        "//src/core:lib/transport/transport_impl.h",
    ] +
    # TODO(vigneshbabu): remove these
    # These headers used to be vended by this target, but they have to be
    # removed after landing EventEngine.
    [
        "//src/core:lib/iomgr/event_engine_shims/closure.h",
        "//src/core:lib/iomgr/event_engine_shims/endpoint.h",
        "//src/core:lib/iomgr/event_engine_shims/tcp_client.h",
    ],
    defines = select({
        "systemd": ["HAVE_LIBSYSTEMD"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:optional",
        "absl/types:variant",
        "absl/utility",
        "madler_zlib",
    ],
    language = "c++",
    linkopts = select({
        "systemd": ["-lsystemd"],
        "//conditions:default": [],
    }),
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "config_vars",
        "cpp_impl_of",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
        "iomgr_buffer_list",
        "iomgr_internal_errqueue",
        "iomgr_timer",
        "legacy_context",
        "orphanable",
        "parse_address",
        "promise",
        "ref_counted_ptr",
        "sockaddr_utils",
        "stats",
        "uri_parser",
        "work_serializer",
        "//src/core:1999",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:atomic_utils",
        "//src/core:bitset",
        "//src/core:cancel_callback",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:chunked_vector",
        "//src/core:closure",
        "//src/core:construct_destruct",
        "//src/core:context",
        "//src/core:default_event_engine",
        "//src/core:dual_ref_counted",
        "//src/core:error",
        "//src/core:event_engine_common",
        "//src/core:event_engine_memory_allocator_factory",
        "//src/core:event_engine_shim",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:event_engine_trace",
        "//src/core:event_log",
        "//src/core:experiments",
        "//src/core:for_each",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:gpr_spinlock",
        "//src/core:grpc_sockaddr",
        "//src/core:http2_errors",
        "//src/core:if",
        "//src/core:if_list",
        "//src/core:init_internally",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/core:latch",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:match",
        "//src/core:memory_quota",
        "//src/core:metadata_compression_traits",
        "//src/core:no_destruct",
        "//src/core:packed_table",
        "//src/core:per_cpu",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:pollset_set",
        "//src/core:posix_event_engine_base_hdrs",
        "//src/core:promise_status",
        "//src/core:race",
        "//src/core:ref_counted",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:resource_quota_trace",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_cast",
        "//src/core:slice_refcount",
        "//src/core:socket_mutator",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:strerror",
        "//src/core:thread_quota",
        "//src/core:time",
        "//src/core:transport_fwd",
        "//src/core:try_seq",
        "//src/core:type_list",
        "//src/core:useful",
        "//src/core:windows_event_engine",
        "//src/core:windows_event_engine_listener",
    ],
)

grpc_cc_library(
    name = "legacy_context",
    hdrs = [
        "//src/core:lib/channel/context.h",
    ],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "//src/core:context",
    ],
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
        "gpr",
        "gpr_platform",
        "grpc++",
        "//src/core:grpc_sockaddr",
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
        "gpr_platform",
        "grpc++",
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
    tags = ["nofixdeps"],
    deps = [
        "channel_arg_names",
        "gpr",
        "gpr_platform",
        "grpc",
        "grpc++",
        "grpc++_public_hdrs",
        "grpc_public_hdrs",
        "lb_server_load_reporting_service_server_builder_plugin",
        "//src/core:lb_server_load_reporting_filter",
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
    external_deps = [
        "absl/memory",
        "protobuf_headers",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "grpc++",
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
        "gpr",
        "gpr_platform",
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
        "opencensus-tags",
        "protobuf_headers",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "lb_get_cpu_stats",
        "lb_load_data_store",
        "//src/proto/grpc/lb/v1:load_reporter_proto",
    ],
)

grpc_cc_library(
    name = "grpc_security_base",
    srcs = [
        "//src/core:lib/security/context/security_context.cc",
        "//src/core:lib/security/credentials/call_creds_util.cc",
        "//src/core:lib/security/credentials/composite/composite_credentials.cc",
        "//src/core:lib/security/credentials/credentials.cc",
        "//src/core:lib/security/credentials/plugin/plugin_credentials.cc",
        "//src/core:lib/security/security_connector/security_connector.cc",
        "//src/core:lib/security/transport/client_auth_filter.cc",
        "//src/core:lib/security/transport/secure_endpoint.cc",
        "//src/core:lib/security/transport/security_handshaker.cc",
        "//src/core:lib/security/transport/server_auth_filter.cc",
        "//src/core:lib/security/transport/tsi_error.cc",
    ],
    hdrs = [
        "//src/core:lib/security/context/security_context.h",
        "//src/core:lib/security/credentials/call_creds_util.h",
        "//src/core:lib/security/credentials/composite/composite_credentials.h",
        "//src/core:lib/security/credentials/credentials.h",
        "//src/core:lib/security/credentials/plugin/plugin_credentials.h",
        "//src/core:lib/security/security_connector/security_connector.h",
        "//src/core:lib/security/transport/auth_filters.h",
        "//src/core:lib/security/transport/secure_endpoint.h",
        "//src/core:lib/security/transport/security_handshaker.h",
        "//src/core:lib/security/transport/tsi_error.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["@grpc:public"],
    deps = [
        "channel_arg_names",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "handshaker",
        "legacy_context",
        "promise",
        "ref_counted_ptr",
        "stats",
        "tsi_base",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:closure",
        "//src/core:context",
        "//src/core:error",
        "//src/core:event_engine_memory_allocator",
        "//src/core:gpr_atm",
        "//src/core:handshaker_factory",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:memory_quota",
        "//src/core:poll",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:resource_quota_trace",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:try_seq",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "tsi_base",
    srcs = [
        "//src/core:tsi/transport_security.cc",
        "//src/core:tsi/transport_security_grpc.cc",
    ],
    hdrs = [
        "//src/core:tsi/transport_security.h",
        "//src/core:tsi/transport_security_grpc.h",
        "//src/core:tsi/transport_security_interface.h",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:tsi_interface"],
    deps = [
        "gpr",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "alts_util",
    srcs = [
        "//src/core:lib/security/credentials/alts/check_gcp_environment.cc",
        "//src/core:lib/security/credentials/alts/check_gcp_environment_linux.cc",
        "//src/core:lib/security/credentials/alts/check_gcp_environment_no_op.cc",
        "//src/core:lib/security/credentials/alts/check_gcp_environment_windows.cc",
        "//src/core:lib/security/credentials/alts/grpc_alts_credentials_client_options.cc",
        "//src/core:lib/security/credentials/alts/grpc_alts_credentials_options.cc",
        "//src/core:lib/security/credentials/alts/grpc_alts_credentials_server_options.cc",
        "//src/core:tsi/alts/handshaker/transport_security_common_api.cc",
    ],
    hdrs = [
        "include/grpc/grpc_security.h",
        "//src/core:lib/security/credentials/alts/check_gcp_environment.h",
        "//src/core:lib/security/credentials/alts/grpc_alts_credentials_options.h",
        "//src/core:tsi/alts/handshaker/transport_security_common_api.h",
    ],
    external_deps = ["upb_lib"],
    language = "c++",
    visibility = ["@grpc:tsi"],
    deps = [
        "alts_upb",
        "gpr",
        "grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "tsi",
    external_deps = [
        "libssl",
        "libcrypto",
        "absl/strings",
        "upb_lib",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:tsi"],
    deps = [
        "gpr",
        "tsi_alts_frame_protector",
        "tsi_base",
        "tsi_fake_credentials",
        "//src/core:tsi_local_credentials",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc++_base",
    srcs = GRPCXX_SRCS + [
        "src/cpp/client/insecure_credentials.cc",
        "src/cpp/client/secure_credentials.cc",
        "src/cpp/common/auth_property_iterator.cc",
        "src/cpp/common/secure_auth_context.cc",
        "src/cpp/common/secure_channel_arguments.cc",
        "src/cpp/common/secure_create_auth_context.cc",
        "src/cpp/common/tls_certificate_provider.cc",
        "src/cpp/common/tls_certificate_verifier.cc",
        "src/cpp/common/tls_credentials_options.cc",
        "src/cpp/server/insecure_server_credentials.cc",
        "src/cpp/server/secure_server_credentials.cc",
    ],
    hdrs = GRPCXX_HDRS + [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/memory",
        "absl/types:optional",
        "upb_lib",
        "protobuf_headers",
        "absl/container:inlined_vector",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = ["nofixdeps"],
    visibility = ["@grpc:alt_grpc++_base_legacy"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "gpr",
        "grpc",
        "grpc++_codegen_proto",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_health_upb",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpcpp_backend_metric_recorder",
        "grpcpp_call_metric_recorder",
        "grpcpp_status",
        "iomgr_timer",
        "legacy_context",
        "ref_counted_ptr",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_backend_metric_provider",
        "//src/core:grpc_service_config",
        "//src/core:grpc_transport_inproc",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:socket_mutator",
        "//src/core:status_helper",
        "//src/core:thread_quota",
        "//src/core:time",
        "//src/core:useful",
    ],
)

# TODO(chengyuc): Give it another try to merge this to `grpc++_base` after
# codegen files are removed.
grpc_cc_library(
    name = "grpc++_base_unsecure",
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
        "absl/types:optional",
        "absl/memory",
        "upb_lib",
        "absl/strings:str_format",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["@grpc:alt_grpc++_base_unsecure_legacy"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_health_upb",
        "grpc_public_hdrs",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_unsecure",
        "grpcpp_backend_metric_recorder",
        "grpcpp_call_metric_recorder",
        "grpcpp_status",
        "iomgr_timer",
        "legacy_context",
        "ref_counted_ptr",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_backend_metric_provider",
        "//src/core:grpc_insecure_credentials",
        "//src/core:grpc_service_config",
        "//src/core:grpc_transport_inproc",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:socket_mutator",
        "//src/core:time",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_proto",
    external_deps = [
        "absl/strings:cord",
        "protobuf_headers",
        "protobuf",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/proto_utils.h",
        "include/grpcpp/impl/codegen/proto_buffer_reader.h",
        "include/grpcpp/impl/codegen/proto_buffer_writer.h",
        "include/grpcpp/impl/codegen/proto_utils.h",
        "include/grpcpp/impl/proto_utils.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
    deps = [
        "grpc++_config_proto",
        "grpc++_public_hdrs",
        "grpcpp_status",
    ],
)

grpc_cc_library(
    name = "grpc++_config_proto",
    external_deps = [
        "protobuf_headers",
        "protobuf",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/impl/codegen/config_protobuf.h",
        "include/grpcpp/impl/codegen/config_protobuf.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
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
    external_deps = [
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc++/ext/proto_server_reflection_plugin.h",
        "include/grpcpp/ext/proto_server_reflection_plugin.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
    deps = [
        "grpc++",
        "grpc++_config_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_call_metric_recorder",
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/call_metric_recorder.h",
    ],
    visibility = ["@grpc:public"],
    deps = ["grpc++_public_hdrs"],
)

grpc_cc_library(
    name = "grpcpp_backend_metric_recorder",
    srcs = [
        "src/cpp/server/backend_metric_recorder.cc",
    ],
    hdrs = [
        "src/cpp/server/backend_metric_recorder.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/server_metric_recorder.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc++_public_hdrs",
        "grpc_trace",
        "grpcpp_call_metric_recorder",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_backend_metric_provider",
    ],
)

grpc_cc_library(
    name = "grpcpp_orca_service",
    srcs = [
        "src/cpp/server/orca/orca_service.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
        "absl/time",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/orca_service.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc++",
        "grpc_base",
        "grpcpp_backend_metric_recorder",
        "protobuf_duration_upb",
        "ref_counted_ptr",
        "xds_orca_service_upb",
        "xds_orca_upb",
        "//src/core:default_event_engine",
        "//src/core:grpc_backend_metric_data",
        "//src/core:ref_counted",
        "//src/core:time",
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
    external_deps = [
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/channelz_service_plugin.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:channelz"],
    deps = [
        "gpr",
        "grpc",
        "grpc++",
        "grpc++_config_proto",
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
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "grpc",
        "grpc++_base",
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
        ":grpc_no_xds": ["GRPC_NO_XDS"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/memory",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/admin_services.h",
    ],
    select_deps = [{
        ":grpc_no_xds": [],
        "//conditions:default": ["//:grpcpp_csds"],
    }],
    deps = [
        "gpr",
        "grpc++",
        "grpcpp_channelz",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpc++_test",
    testonly = True,
    srcs = [
        "src/cpp/client/channel_test_peer.cc",
    ],
    external_deps = ["gtest"],
    public_hdrs = [
        "include/grpc++/test/mock_stream.h",
        "include/grpc++/test/server_context_test_spouse.h",
        "include/grpcpp/test/channel_test_peer.h",
        "include/grpcpp/test/client_context_test_peer.h",
        "include/grpcpp/test/default_reactor_test_peer.h",
        "include/grpcpp/test/mock_stream.h",
        "include/grpcpp/test/server_context_test_spouse.h",
    ],
    visibility = ["@grpc:grpc++_test"],
    deps = [
        "grpc++",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_rpc_encoding",
    srcs = [
        "src/cpp/ext/filters/census/rpc_encoding.cc",
    ],
    hdrs = [
        "src/cpp/ext/filters/census/rpc_encoding.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/meta:type_traits",
        "absl/status",
        "absl/strings",
        "absl/time",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:grpc_python_observability"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_opencensus_plugin",
    srcs = [
        "src/cpp/ext/filters/census/client_filter.cc",
        "src/cpp/ext/filters/census/context.cc",
        "src/cpp/ext/filters/census/grpc_plugin.cc",
        "src/cpp/ext/filters/census/measures.cc",
        "src/cpp/ext/filters/census/rpc_encoding.cc",
        "src/cpp/ext/filters/census/server_call_tracer.cc",
        "src/cpp/ext/filters/census/views.cc",
    ],
    hdrs = [
        "include/grpcpp/opencensus.h",
        "src/cpp/ext/filters/census/client_filter.h",
        "src/cpp/ext/filters/census/context.h",
        "src/cpp/ext/filters/census/grpc_plugin.h",
        "src/cpp/ext/filters/census/measures.h",
        "src/cpp/ext/filters/census/open_census_call_tracer.h",
        "src/cpp/ext/filters/census/rpc_encoding.h",
        "src/cpp/ext/filters/census/server_call_tracer.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:optional",
        "opencensus-stats",
        "opencensus-tags",
        "opencensus-tags-context_util",
        "opencensus-trace",
        "opencensus-trace-context_util",
        "opencensus-trace-propagation",
        "opencensus-trace-span_context",
    ],
    language = "c++",
    visibility = ["@grpc:grpc_opencensus_plugin"],
    deps = [
        "channel_stack_builder",
        "config",
        "gpr",
        "grpc++_base",
        "grpc_base",
        "grpc_public_hdrs",
        "legacy_context",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:context",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
    ],
)

# This is an EXPERIMENTAL target subject to change.
grpc_cc_library(
    name = "grpcpp_gcp_observability",
    hdrs = [
        "include/grpcpp/ext/gcp_observability.h",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:grpcpp_gcp_observability"],
    deps = [
        "//src/cpp/ext/gcp:observability",
    ],
)

grpc_cc_library(
    name = "work_serializer",
    srcs = [
        "//src/core:lib/gprpp/work_serializer.cc",
    ],
    hdrs = [
        "//src/core:lib/gprpp/work_serializer.h",
    ],
    external_deps = ["absl/base:core_headers"],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "debug_location",
        "gpr",
        "grpc_trace",
        "orphanable",
    ],
)

grpc_cc_library(
    name = "grpc_trace",
    srcs = ["//src/core:lib/debug/trace.cc"],
    hdrs = ["//src/core:lib/debug/trace.h"],
    external_deps = ["absl/strings"],
    language = "c++",
    visibility = ["@grpc:trace"],
    deps = [
        "config_vars",
        "gpr",
        "grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "load_config",
    srcs = [
        "//src/core:lib/config/load_config.cc",
    ],
    hdrs = [
        "//src/core:lib/config/load_config.h",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:marshalling",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "gpr_platform",
        "//src/core:env",
        "//src/core:gpr_log_internal",
    ],
)

grpc_cc_library(
    name = "config_vars",
    srcs = [
        "//src/core:lib/config/config_vars.cc",
        "//src/core:lib/config/config_vars_non_generated.cc",
    ],
    hdrs = [
        "//src/core:lib/config/config_vars.h",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "gpr_platform",
        "load_config",
    ],
)

grpc_cc_library(
    name = "config",
    srcs = [
        "//src/core:lib/config/core_configuration.cc",
    ],
    language = "c++",
    public_hdrs = [
        "//src/core:lib/config/core_configuration.h",
    ],
    visibility = ["@grpc:client_channel"],
    deps = [
        "gpr",
        "grpc_resolver",
        "//src/core:certificate_provider_registry",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_creds_registry",
        "//src/core:channel_init",
        "//src/core:handshaker_registry",
        "//src/core:lb_policy_registry",
        "//src/core:proxy_mapper_registry",
        "//src/core:service_config_parser",
    ],
)

grpc_cc_library(
    name = "debug_location",
    language = "c++",
    public_hdrs = ["//src/core:lib/gprpp/debug_location.h"],
    visibility = ["@grpc:debug_location"],
)

grpc_cc_library(
    name = "orphanable",
    language = "c++",
    public_hdrs = ["//src/core:lib/gprpp/orphanable.h"],
    visibility = [
        "@grpc:client_channel",
        "@grpc:xds_client_core",
    ],
    deps = [
        "debug_location",
        "gpr_platform",
        "ref_counted_ptr",
        "//src/core:ref_counted",
    ],
)

grpc_cc_library(
    name = "promise",
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "//src/core:lib/promise/promise.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "//src/core:poll",
        "//src/core:promise_like",
    ],
)

grpc_cc_library(
    name = "ref_counted_ptr",
    language = "c++",
    public_hdrs = ["//src/core:lib/gprpp/ref_counted_ptr.h"],
    visibility = ["@grpc:ref_counted_ptr"],
    deps = [
        "debug_location",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "handshaker",
    srcs = [
        "//src/core:lib/transport/handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/status",
        "absl/strings:str_format",
    ],
    language = "c++",
    public_hdrs = [
        "//src/core:lib/transport/handshaker.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "debug_location",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_helper",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "http_connect_handshaker",
    srcs = [
        "//src/core:lib/transport/http_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "//src/core:lib/transport/http_connect_handshaker.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "handshaker",
        "httpcli",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:handshaker_factory",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_library(
    name = "exec_ctx",
    srcs = [
        "//src/core:lib/iomgr/combiner.cc",
        "//src/core:lib/iomgr/exec_ctx.cc",
        "//src/core:lib/iomgr/executor.cc",
        "//src/core:lib/iomgr/iomgr_internal.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/combiner.h",
        "//src/core:lib/iomgr/exec_ctx.h",
        "//src/core:lib/iomgr/executor.h",
        "//src/core:lib/iomgr/iomgr_internal.h",
    ],
    external_deps = ["absl/strings:str_format"],
    visibility = [
        "@grpc:alt_grpc_base_legacy",
        "@grpc:exec_ctx",
    ],
    deps = [
        "debug_location",
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:gpr_spinlock",
        "//src/core:time",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "sockaddr_utils",
    srcs = [
        "//src/core:lib/address_utils/sockaddr_utils.cc",
    ],
    hdrs = [
        "//src/core:lib/address_utils/sockaddr_utils.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "uri_parser",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
    ],
)

grpc_cc_library(
    name = "iomgr_timer",
    srcs = [
        "//src/core:lib/iomgr/timer.cc",
        "//src/core:lib/iomgr/timer_generic.cc",
        "//src/core:lib/iomgr/timer_heap.cc",
        "//src/core:lib/iomgr/timer_manager.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/timer.h",
        "//src/core:lib/iomgr/timer_generic.h",
        "//src/core:lib/iomgr/timer_heap.h",
        "//src/core:lib/iomgr/timer_manager.h",
    ] + [
        # TODO(hork): deduplicate
        "//src/core:lib/iomgr/iomgr.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:iomgr_timer"],
    deps = [
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "gpr_platform",
        "grpc_trace",
        "//src/core:closure",
        "//src/core:gpr_manual_constructor",
        "//src/core:gpr_spinlock",
        "//src/core:iomgr_port",
        "//src/core:time",
        "//src/core:time_averaged_stats",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "iomgr_internal_errqueue",
    srcs = [
        "//src/core:lib/iomgr/internal_errqueue.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/internal_errqueue.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:iomgr_internal_errqueue"],
    deps = [
        "gpr",
        "//src/core:iomgr_port",
        "//src/core:strerror",
    ],
)

grpc_cc_library(
    name = "iomgr_buffer_list",
    srcs = [
        "//src/core:lib/iomgr/buffer_list.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/buffer_list.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:iomgr_buffer_list"],
    deps = [
        "gpr",
        "iomgr_internal_errqueue",
        "//src/core:error",
        "//src/core:iomgr_port",
    ],
)

grpc_cc_library(
    name = "uri_parser",
    srcs = [
        "//src/core:lib/uri/uri_parser.cc",
    ],
    hdrs = [
        "//src/core:lib/uri/uri_parser.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "parse_address",
    srcs = [
        "//src/core:lib/address_utils/parse_address.cc",
        "//src/core:lib/iomgr/grpc_if_nametoindex_posix.cc",
        "//src/core:lib/iomgr/grpc_if_nametoindex_unsupported.cc",
    ],
    hdrs = [
        "//src/core:lib/address_utils/parse_address.h",
        "//src/core:lib/iomgr/grpc_if_nametoindex.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "uri_parser",
        "//src/core:error",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//src/core:status_helper",
    ],
)

grpc_cc_library(
    name = "backoff",
    srcs = [
        "//src/core:lib/backoff/backoff.cc",
    ],
    hdrs = [
        "//src/core:lib/backoff/backoff.h",
    ],
    external_deps = ["absl/random"],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "stats",
    srcs = [
        "//src/core:lib/debug/stats.cc",
    ],
    hdrs = [
        "//src/core:lib/debug/stats.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:span",
    ],
    visibility = [
        "@grpc:alt_grpc_base_legacy",
    ],
    deps = [
        "gpr",
        "//src/core:histogram_view",
        "//src/core:no_destruct",
        "//src/core:stats_data",
    ],
)

grpc_cc_library(
    name = "channel_stack_builder",
    srcs = [
        "//src/core:lib/channel/channel_stack_builder.cc",
    ],
    hdrs = [
        "//src/core:lib/channel/channel_stack_builder.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:transport_fwd",
    ],
)

grpc_cc_library(
    name = "grpc_service_config_impl",
    srcs = [
        "//src/core:lib/service_config/service_config_impl.cc",
    ],
    hdrs = [
        "//src/core:lib/service_config/service_config_impl.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "config",
        "gpr",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//src/core:json",
        "//src/core:json_args",
        "//src/core:json_object_loader",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:service_config_parser",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:validation_errors",
    ],
)

grpc_cc_library(
    name = "server_address",
    srcs = [
        "//src/core:lib/resolver/server_address.cc",
    ],
    hdrs = [
        "//src/core:lib/resolver/server_address.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "gpr_platform",
        "sockaddr_utils",
        "//src/core:channel_args",
        "//src/core:resolved_address",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_resolver",
    srcs = [
        "//src/core:lib/resolver/resolver.cc",
        "//src/core:lib/resolver/resolver_registry.cc",
    ],
    hdrs = [
        "//src/core:lib/resolver/resolver.h",
        "//src/core:lib/resolver/resolver_factory.h",
        "//src/core:lib/resolver/resolver_registry.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "gpr",
        "grpc_trace",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "uri_parser",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//src/core:iomgr_fwd",
    ],
)

grpc_cc_library(
    name = "grpc_client_channel",
    srcs = [
        "//src/core:ext/filters/client_channel/backend_metric.cc",
        "//src/core:ext/filters/client_channel/backup_poller.cc",
        "//src/core:ext/filters/client_channel/channel_connectivity.cc",
        "//src/core:ext/filters/client_channel/client_channel.cc",
        "//src/core:ext/filters/client_channel/client_channel_channelz.cc",
        "//src/core:ext/filters/client_channel/client_channel_factory.cc",
        "//src/core:ext/filters/client_channel/client_channel_plugin.cc",
        "//src/core:ext/filters/client_channel/client_channel_service_config.cc",
        "//src/core:ext/filters/client_channel/config_selector.cc",
        "//src/core:ext/filters/client_channel/dynamic_filters.cc",
        "//src/core:ext/filters/client_channel/global_subchannel_pool.cc",
        "//src/core:ext/filters/client_channel/http_proxy.cc",
        "//src/core:ext/filters/client_channel/lb_policy/child_policy_handler.cc",
        "//src/core:ext/filters/client_channel/lb_policy/oob_backend_metric.cc",
        "//src/core:ext/filters/client_channel/local_subchannel_pool.cc",
        "//src/core:ext/filters/client_channel/retry_filter.cc",
        "//src/core:ext/filters/client_channel/retry_filter_legacy_call_data.cc",
        "//src/core:ext/filters/client_channel/retry_service_config.cc",
        "//src/core:ext/filters/client_channel/retry_throttle.cc",
        "//src/core:ext/filters/client_channel/service_config_channel_arg_filter.cc",
        "//src/core:ext/filters/client_channel/subchannel.cc",
        "//src/core:ext/filters/client_channel/subchannel_pool_interface.cc",
        "//src/core:ext/filters/client_channel/subchannel_stream_client.cc",
    ],
    hdrs = [
        "//src/core:ext/filters/client_channel/backend_metric.h",
        "//src/core:ext/filters/client_channel/backup_poller.h",
        "//src/core:ext/filters/client_channel/client_channel.h",
        "//src/core:ext/filters/client_channel/client_channel_channelz.h",
        "//src/core:ext/filters/client_channel/client_channel_factory.h",
        "//src/core:ext/filters/client_channel/client_channel_internal.h",
        "//src/core:ext/filters/client_channel/client_channel_service_config.h",
        "//src/core:ext/filters/client_channel/config_selector.h",
        "//src/core:ext/filters/client_channel/connector.h",
        "//src/core:ext/filters/client_channel/dynamic_filters.h",
        "//src/core:ext/filters/client_channel/global_subchannel_pool.h",
        "//src/core:ext/filters/client_channel/http_proxy.h",
        "//src/core:ext/filters/client_channel/lb_policy/child_policy_handler.h",
        "//src/core:ext/filters/client_channel/lb_policy/oob_backend_metric.h",
        "//src/core:ext/filters/client_channel/lb_policy/oob_backend_metric_internal.h",
        "//src/core:ext/filters/client_channel/local_subchannel_pool.h",
        "//src/core:ext/filters/client_channel/retry_filter.h",
        "//src/core:ext/filters/client_channel/retry_filter_legacy_call_data.h",
        "//src/core:ext/filters/client_channel/retry_service_config.h",
        "//src/core:ext/filters/client_channel/retry_throttle.h",
        "//src/core:ext/filters/client_channel/subchannel.h",
        "//src/core:ext/filters/client_channel/subchannel_interface_internal.h",
        "//src/core:ext/filters/client_channel/subchannel_pool_interface.h",
        "//src/core:ext/filters/client_channel/subchannel_stream_client.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
        "absl/types:optional",
        "absl/types:variant",
        "upb_collections_lib",
        "upb_lib",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "backoff",
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "config_vars",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "http_connect_handshaker",
        "iomgr_timer",
        "legacy_context",
        "orphanable",
        "parse_address",
        "protobuf_duration_upb",
        "ref_counted_ptr",
        "server_address",
        "sockaddr_utils",
        "stats",
        "uri_parser",
        "work_serializer",
        "xds_orca_service_upb",
        "xds_orca_upb",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:construct_destruct",
        "//src/core:context",
        "//src/core:delegating_helper",
        "//src/core:dual_ref_counted",
        "//src/core:env",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_deadline_filter",
        "//src/core:grpc_service_config",
        "//src/core:init_internally",
        "//src/core:iomgr_fwd",
        "//src/core:json",
        "//src/core:json_args",
        "//src/core:json_channel_args",
        "//src/core:json_object_loader",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:memory_quota",
        "//src/core:pollset_set",
        "//src/core:proxy_mapper",
        "//src/core:proxy_mapper_registry",
        "//src/core:ref_counted",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:service_config_parser",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:subchannel_interface",
        "//src/core:time",
        "//src/core:transport_fwd",
        "//src/core:unique_type_name",
        "//src/core:useful",
        "//src/core:validation_errors",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc",
    ],
    hdrs = [
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.h",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "//src/core:ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "address_sorting",
        "cares",
    ],
    language = "c++",
    deps = [
        "backoff",
        "channel_arg_names",
        "config",
        "config_vars",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_grpclb_balancer_addresses",
        "grpc_resolver",
        "grpc_service_config_impl",
        "grpc_trace",
        "iomgr_timer",
        "orphanable",
        "parse_address",
        "ref_counted_ptr",
        "server_address",
        "sockaddr_utils",
        "uri_parser",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_service_config",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:polling_resolver",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:service_config_helper",
        "//src/core:slice",
        "//src/core:status_helper",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "httpcli",
    srcs = [
        "//src/core:lib/http/format_request.cc",
        "//src/core:lib/http/httpcli.cc",
        "//src/core:lib/http/parser.cc",
    ],
    hdrs = [
        "//src/core:lib/http/format_request.h",
        "//src/core:lib/http/httpcli.h",
        "//src/core:lib/http/parser.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:httpcli"],
    deps = [
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_trace",
        "handshaker",
        "orphanable",
        "ref_counted_ptr",
        "sockaddr_utils",
        "uri_parser",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:tcp_connect_handshaker",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "grpc_alts_credentials",
    srcs = [
        "//src/core:lib/security/credentials/alts/alts_credentials.cc",
        "//src/core:lib/security/security_connector/alts/alts_security_connector.cc",
    ],
    hdrs = [
        "//src/core:lib/security/credentials/alts/alts_credentials.h",
        "//src/core:lib/security/security_connector/alts/alts_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "alts_util",
        "channel_arg_names",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
        "handshaker",
        "promise",
        "ref_counted_ptr",
        "tsi_alts_credentials",
        "tsi_base",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:iomgr_fwd",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "tsi_fake_credentials",
    srcs = [
        "//src/core:tsi/fake_transport_security.cc",
    ],
    hdrs = [
        "//src/core:tsi/fake_transport_security.h",
    ],
    language = "c++",
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "gpr",
        "tsi_base",
        "//src/core:slice",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_jwt_credentials",
    srcs = [
        "//src/core:lib/security/credentials/jwt/json_token.cc",
        "//src/core:lib/security/credentials/jwt/jwt_credentials.cc",
        "//src/core:lib/security/credentials/jwt/jwt_verifier.cc",
    ],
    hdrs = [
        "//src/core:lib/security/credentials/jwt/json_token.h",
        "//src/core:lib/security/credentials/jwt/jwt_credentials.h",
        "//src/core:lib/security/credentials/jwt/jwt_verifier.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:optional",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_trace",
        "httpcli",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "uri_parser",
        "//src/core:arena_promise",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_manual_constructor",
        "//src/core:httpcli_ssl_credentials",
        "//src/core:iomgr_fwd",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:time",
        "//src/core:tsi_ssl_types",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_credentials_util",
    srcs = [
        "//src/core:lib/security/credentials/tls/tls_utils.cc",
        "//src/core:lib/security/security_connector/load_system_roots_fallback.cc",
        "//src/core:lib/security/security_connector/load_system_roots_supported.cc",
        "//src/core:lib/security/util/json_util.cc",
    ],
    hdrs = [
        "//src/core:lib/security/credentials/tls/tls_utils.h",
        "//src/core:lib/security/security_connector/load_system_roots.h",
        "//src/core:lib/security/security_connector/load_system_roots_supported.h",
        "//src/core:lib/security/util/json_util.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "config_vars",
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "//src/core:error",
        "//src/core:json",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "tsi_alts_credentials",
    srcs = [
        "//src/core:tsi/alts/handshaker/alts_handshaker_client.cc",
        "//src/core:tsi/alts/handshaker/alts_shared_resource.cc",
        "//src/core:tsi/alts/handshaker/alts_tsi_handshaker.cc",
        "//src/core:tsi/alts/handshaker/alts_tsi_utils.cc",
    ],
    hdrs = [
        "//src/core:tsi/alts/handshaker/alts_handshaker_client.h",
        "//src/core:tsi/alts/handshaker/alts_shared_resource.h",
        "//src/core:tsi/alts/handshaker/alts_tsi_handshaker.h",
        "//src/core:tsi/alts/handshaker/alts_tsi_handshaker_private.h",
        "//src/core:tsi/alts/handshaker/alts_tsi_utils.h",
    ],
    external_deps = [
        "absl/strings",
        "upb_lib",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "alts_upb",
        "alts_util",
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "tsi_alts_frame_protector",
        "tsi_base",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:env",
        "//src/core:pollset_set",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "tsi_alts_frame_protector",
    srcs = [
        "//src/core:tsi/alts/crypt/aes_gcm.cc",
        "//src/core:tsi/alts/crypt/gsec.cc",
        "//src/core:tsi/alts/frame_protector/alts_counter.cc",
        "//src/core:tsi/alts/frame_protector/alts_crypter.cc",
        "//src/core:tsi/alts/frame_protector/alts_frame_protector.cc",
        "//src/core:tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc",
        "//src/core:tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc",
        "//src/core:tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc",
        "//src/core:tsi/alts/frame_protector/frame_handler.cc",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc",
    ],
    hdrs = [
        "//src/core:tsi/alts/crypt/gsec.h",
        "//src/core:tsi/alts/frame_protector/alts_counter.h",
        "//src/core:tsi/alts/frame_protector/alts_crypter.h",
        "//src/core:tsi/alts/frame_protector/alts_frame_protector.h",
        "//src/core:tsi/alts/frame_protector/alts_record_protocol_crypter_common.h",
        "//src/core:tsi/alts/frame_protector/frame_handler.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h",
        "//src/core:tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h",
    ],
    external_deps = [
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "gpr_platform",
        "tsi_base",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_session_cache",
    srcs = [
        "//src/core:tsi/ssl/session_cache/ssl_session_boringssl.cc",
        "//src/core:tsi/ssl/session_cache/ssl_session_cache.cc",
        "//src/core:tsi/ssl/session_cache/ssl_session_openssl.cc",
    ],
    hdrs = [
        "//src/core:tsi/ssl/session_cache/ssl_session.h",
        "//src/core:tsi/ssl/session_cache/ssl_session_cache.h",
    ],
    external_deps = [
        "absl/memory",
        "libssl",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "cpp_impl_of",
        "gpr",
        "grpc_public_hdrs",
        "//src/core:ref_counted",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_credentials",
    srcs = [
        "//src/core:lib/security/security_connector/ssl_utils.cc",
        "//src/core:tsi/ssl/key_logging/ssl_key_logging.cc",
        "//src/core:tsi/ssl_transport_security.cc",
        "//src/core:tsi/ssl_transport_security_utils.cc",
    ],
    hdrs = [
        "//src/core:lib/security/security_connector/ssl_utils.h",
        "//src/core:tsi/ssl/key_logging/ssl_key_logging.h",
        "//src/core:tsi/ssl_transport_security.h",
        "//src/core:tsi/ssl_transport_security_utils.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "channel_arg_names",
        "config_vars",
        "gpr",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_public_hdrs",
        "grpc_security_base",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_ssl_session_cache",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:grpc_transport_chttp2_alpn",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:tsi_ssl_types",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_http_filters",
    srcs = [
        "//src/core:ext/filters/http/client/http_client_filter.cc",
        "//src/core:ext/filters/http/http_filters_plugin.cc",
        "//src/core:ext/filters/http/message_compress/compression_filter.cc",
        "//src/core:ext/filters/http/server/http_server_filter.cc",
    ],
    hdrs = [
        "//src/core:ext/filters/http/client/http_client_filter.h",
        "//src/core:ext/filters/http/message_compress/compression_filter.h",
        "//src/core:ext/filters/http/server/http_server_filter.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:http"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "legacy_context",
        "promise",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:context",
        "//src/core:grpc_message_size_filter",
        "//src/core:latch",
        "//src/core:map",
        "//src/core:percent_encoding",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:prioritized_race",
        "//src/core:race",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:transport_fwd",
    ],
)

grpc_cc_library(
    name = "grpc_grpclb_balancer_addresses",
    srcs = [
        "//src/core:ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.cc",
    ],
    hdrs = [
        "//src/core:ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h",
    ],
    language = "c++",
    visibility = ["@grpc:grpclb"],
    deps = [
        "gpr_platform",
        "grpc_public_hdrs",
        "server_address",
        "//src/core:channel_args",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "xds_client",
    srcs = [
        "//src/core:ext/xds/xds_api.cc",
        "//src/core:ext/xds/xds_bootstrap.cc",
        "//src/core:ext/xds/xds_client.cc",
        "//src/core:ext/xds/xds_client_stats.cc",
    ],
    hdrs = [
        "//src/core:ext/xds/xds_api.h",
        "//src/core:ext/xds/xds_bootstrap.h",
        "//src/core:ext/xds/xds_channel_args.h",
        "//src/core:ext/xds/xds_client.h",
        "//src/core:ext/xds/xds_client_stats.h",
        "//src/core:ext/xds/xds_resource_type.h",
        "//src/core:ext/xds/xds_resource_type_impl.h",
        "//src/core:ext/xds/xds_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "upb_lib",
        "upb_textformat_lib",
        "upb_json_lib",
        "upb_reflection",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:xds_client_core"],
    deps = [
        "backoff",
        "debug_location",
        "envoy_admin_upb",
        "envoy_config_core_upb",
        "envoy_config_endpoint_upb",
        "envoy_service_discovery_upb",
        "envoy_service_discovery_upbdefs",
        "envoy_service_load_stats_upb",
        "envoy_service_load_stats_upbdefs",
        "envoy_service_status_upb",
        "envoy_service_status_upbdefs",
        "event_engine_base_hdrs",
        "exec_ctx",
        "google_rpc_status_upb",
        "gpr",
        "grpc_trace",
        "orphanable",
        "protobuf_any_upb",
        "protobuf_duration_upb",
        "protobuf_struct_upb",
        "protobuf_timestamp_upb",
        "ref_counted_ptr",
        "uri_parser",
        "work_serializer",
        "//src/core:default_event_engine",
        "//src/core:dual_ref_counted",
        "//src/core:env",
        "//src/core:json",
        "//src/core:per_cpu",
        "//src/core:ref_counted",
        "//src/core:time",
        "//src/core:upb_utils",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_mock_cel",
    hdrs = [
        "//src/core:lib/security/authorization/mock_cel/activation.h",
        "//src/core:lib/security/authorization/mock_cel/cel_expr_builder_factory.h",
        "//src/core:lib/security/authorization/mock_cel/cel_expression.h",
        "//src/core:lib/security/authorization/mock_cel/cel_value.h",
        "//src/core:lib/security/authorization/mock_cel/evaluator_core.h",
        "//src/core:lib/security/authorization/mock_cel/flat_expr_builder.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    language = "c++",
    deps = [
        "google_type_expr_upb",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["//src/core:ext/filters/client_channel/resolver/fake/fake_resolver.cc"],
    hdrs = ["//src/core:ext/filters/client_channel/resolver/fake/fake_resolver.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = [
        "//test:__subpackages__",
        "@grpc:grpc_resolver_fake",
    ],
    deps = [
        "config",
        "debug_location",
        "gpr",
        "grpc_public_hdrs",
        "grpc_resolver",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "uri_parser",
        "work_serializer",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//src/core:ref_counted",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "chttp2_frame",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/frame.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/frame.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
        "absl/types:variant",
    ],
    deps = [
        "gpr",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_library(
    name = "chttp2_legacy_frame",
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/legacy_frame.h",
    ],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "http_trace",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/http_trace.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/http_trace.h",
    ],
    deps = [
        "gpr_platform",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "hpack_parser_table",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser_table.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser_table.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "gpr",
        "gpr_platform",
        "grpc_base",
        "grpc_trace",
        "hpack_parse_result",
        "http_trace",
        "//src/core:hpack_constants",
        "//src/core:no_destruct",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "hpack_parse_result",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parse_result.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parse_result.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "gpr",
        "grpc_base",
        "//src/core:error",
        "//src/core:hpack_constants",
        "//src/core:slice",
        "//src/core:status_helper",
    ],
)

grpc_cc_library(
    name = "hpack_parser",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/types:optional",
        "absl/types:span",
        "absl/types:variant",
    ],
    deps = [
        "chttp2_legacy_frame",
        "gpr",
        "gpr_platform",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "hpack_parse_result",
        "hpack_parser_table",
        "stats",
        "//src/core:decode_huff",
        "//src/core:error",
        "//src/core:hpack_constants",
        "//src/core:match",
        "//src/core:random_early_detection",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
    ],
)

grpc_cc_library(
    name = "hpack_encoder",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/hpack_encoder.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/hpack_encoder.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "chttp2_bin_encoder",
        "chttp2_legacy_frame",
        "chttp2_varint",
        "gpr",
        "gpr_platform",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "http_trace",
        "//src/core:hpack_constants",
        "//src/core:hpack_encoder_table",
        "//src/core:metadata_compression_traits",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "chttp2_bin_encoder",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/bin_encoder.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/bin_encoder.h",
    ],
    deps = [
        "gpr",
        "gpr_platform",
        "//src/core:huffsyms",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "chttp2_varint",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/varint.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/varint.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "chttp2_context_list_entry",
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/context_list_entry.h",
    ],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/bin_decoder.cc",
        "//src/core:ext/transport/chttp2/transport/chttp2_transport.cc",
        "//src/core:ext/transport/chttp2/transport/frame_data.cc",
        "//src/core:ext/transport/chttp2/transport/frame_goaway.cc",
        "//src/core:ext/transport/chttp2/transport/frame_ping.cc",
        "//src/core:ext/transport/chttp2/transport/frame_rst_stream.cc",
        "//src/core:ext/transport/chttp2/transport/frame_settings.cc",
        "//src/core:ext/transport/chttp2/transport/frame_window_update.cc",
        "//src/core:ext/transport/chttp2/transport/parsing.cc",
        "//src/core:ext/transport/chttp2/transport/stream_lists.cc",
        "//src/core:ext/transport/chttp2/transport/writing.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/bin_decoder.h",
        "//src/core:ext/transport/chttp2/transport/chttp2_transport.h",
        "//src/core:ext/transport/chttp2/transport/frame_data.h",
        "//src/core:ext/transport/chttp2/transport/frame_goaway.h",
        "//src/core:ext/transport/chttp2/transport/frame_ping.h",
        "//src/core:ext/transport/chttp2/transport/frame_rst_stream.h",
        "//src/core:ext/transport/chttp2/transport/frame_settings.h",
        "//src/core:ext/transport/chttp2/transport/frame_window_update.h",
        "//src/core:ext/transport/chttp2/transport/internal.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/hash",
        "absl/meta:type_traits",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:grpclb"],
    deps = [
        "channel_arg_names",
        "chttp2_context_list_entry",
        "chttp2_legacy_frame",
        "chttp2_varint",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "hpack_encoder",
        "hpack_parser",
        "hpack_parser_table",
        "http_trace",
        "httpcli",
        "iomgr_buffer_list",
        "legacy_context",
        "ref_counted_ptr",
        "stats",
        "//src/core:arena",
        "//src/core:bdp_estimator",
        "//src/core:bitset",
        "//src/core:channel_args",
        "//src/core:chttp2_flow_control",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:http2_errors",
        "//src/core:http2_settings",
        "//src/core:init_internally",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:match",
        "//src/core:memory_quota",
        "//src/core:ping_abuse_policy",
        "//src/core:ping_rate_policy",
        "//src/core:poll",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:resource_quota_trace",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:time",
        "//src/core:transport_fwd",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpcpp_status",
    srcs = [
        "src/cpp/util/status.cc",
    ],
    public_hdrs = [
        "include/grpc++/support/status.h",
        "include/grpcpp/impl/status.h",
        "include/grpcpp/support/status.h",
        "include/grpc++/impl/codegen/status.h",
        "include/grpcpp/impl/codegen/status.h",
    ],
    deps = [
        "gpr_platform",
        "grpc++_public_hdrs",
        "grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "subprocess",
    srcs = [
        "//src/core:lib/gpr/subprocess_posix.cc",
        "//src/core:lib/gpr/subprocess_windows.cc",
    ],
    hdrs = [
        "//src/core:lib/gpr/subprocess.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "gpr",
        "//src/core:strerror",
        "//src/core:tchar",
    ],
)

# TODO(yashykt): Remove the UPB definitions from here once they are no longer needed
### UPB Targets

grpc_upb_proto_library(
    name = "envoy_admin_upb",
    deps = ["@envoy_api//envoy/admin/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_cluster_upb",
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_cluster_upbdefs",
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_core_upb",
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_endpoint_upb",
    deps = ["@envoy_api//envoy/config/endpoint/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_endpoint_upbdefs",
    deps = ["@envoy_api//envoy/config/endpoint/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_listener_upb",
    deps = ["@envoy_api//envoy/config/listener/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_listener_upbdefs",
    deps = ["@envoy_api//envoy/config/listener/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_rbac_upb",
    deps = ["@envoy_api//envoy/config/rbac/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_route_upb",
    deps = ["@envoy_api//envoy/config/route/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_route_upbdefs",
    deps = ["@envoy_api//envoy/config/route/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_clusters_aggregate_upb",
    deps = ["@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_clusters_aggregate_upbdefs",
    deps = ["@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_common_fault_upb",
    deps = ["@envoy_api//envoy/extensions/filters/common/fault/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_fault_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/fault/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_fault_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/fault/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_rbac_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_rbac_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_router_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/router/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_router_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/router/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_stateful_session_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_stateful_session_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_http_stateful_session_cookie_upb",
    deps = ["@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_http_stateful_session_cookie_upbdefs",
    deps = ["@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_http_upb",
    deps = ["@envoy_api//envoy/type/http/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_pick_first_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/pick_first/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_ring_hash_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/ring_hash/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_wrr_locality_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_network_http_connection_manager_upb",
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_network_http_connection_manager_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_transport_sockets_tls_upb",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_transport_sockets_tls_upbdefs",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_discovery_upb",
    deps = ["@envoy_api//envoy/service/discovery/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_discovery_upbdefs",
    deps = ["@envoy_api//envoy/service/discovery/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_load_stats_upb",
    deps = ["@envoy_api//envoy/service/load_stats/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_load_stats_upbdefs",
    deps = ["@envoy_api//envoy/service/load_stats/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_status_upb",
    deps = ["@envoy_api//envoy/service/status/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_status_upbdefs",
    deps = ["@envoy_api//envoy/service/status/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_matcher_upb",
    deps = ["@envoy_api//envoy/type/matcher/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_upb",
    deps = ["@envoy_api//envoy/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_type_upb",
    deps = ["@com_github_cncf_udpa//xds/type/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "xds_type_upbdefs",
    deps = ["@com_github_cncf_udpa//xds/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_upb",
    deps = ["@com_github_cncf_udpa//xds/data/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_service_upb",
    deps = ["@com_github_cncf_udpa//xds/service/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "grpc_health_upb",
    deps = ["//src/proto/grpc/health/v1:health_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "google_rpc_status_upb",
    deps = ["@com_google_googleapis//google/rpc:status_proto"],
)

grpc_upb_proto_reflection_library(
    name = "google_rpc_status_upbdefs",
    deps = ["@com_google_googleapis//google/rpc:status_proto"],
)

grpc_upb_proto_library(
    name = "google_type_expr_upb",
    deps = ["@com_google_googleapis//google/type:expr_proto"],
)

grpc_upb_proto_library(
    name = "grpc_lb_upb",
    deps = ["//src/proto/grpc/lb/v1:load_balancer_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "alts_upb",
    deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
)

grpc_upb_proto_library(
    name = "rls_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "rls_config_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto_descriptor"],
)

grpc_upb_proto_reflection_library(
    name = "rls_config_upbdefs",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto_descriptor"],
)

WELL_KNOWN_PROTO_TARGETS = [
    "any",
    "duration",
    "empty",
    "struct",
    "timestamp",
    "wrappers",
]

[grpc_upb_proto_library(
    name = "protobuf_" + target + "_upb",
    deps = ["@com_google_protobuf//:" + target + "_proto"],
) for target in WELL_KNOWN_PROTO_TARGETS]

[grpc_upb_proto_reflection_library(
    name = "protobuf_" + target + "_upbdefs",
    deps = ["@com_google_protobuf//:" + target + "_proto"],
) for target in WELL_KNOWN_PROTO_TARGETS]

grpc_generate_one_off_targets()

filegroup(
    name = "root_certificates",
    srcs = [
        "etc/roots.pem",
    ],
    visibility = ["//visibility:public"],
)
