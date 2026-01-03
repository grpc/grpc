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

load("@bazel_skylib//lib:selects.bzl", "selects")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load(
    "//bazel:grpc_build_system.bzl",
    "grpc_add_well_known_proto_upb_targets",
    "grpc_cc_library",
    "grpc_clang_cl_settings",
    "grpc_filegroup",
    "grpc_generate_one_off_targets",
    "grpc_upb_proto_library",
    "grpc_upb_proto_reflection_library",
    "python_config_settings",
)

licenses(["reciprocal"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "layering_check",
        "parse_headers",
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

bool_flag(
    name = "small_client",
    build_setting_default = False,
)

config_setting(
    name = "small_client_flag",
    flag_values = {":small_client": "true"},
)

bool_flag(
    name = "exclude_small_client",
    build_setting_default = False,
)

config_setting(
    name = "not_exclude_small_client_flag",  # Negative so it can be used with match_all.
    flag_values = {":exclude_small_client": "false"},
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
    name = "grpc_no_ztrace_define",
    values = {"define": "grpc_no_ztrace=true"},
)

config_setting(
    name = "grpc_experiments_are_final_define",
    values = {"define": "grpc_experiments_are_final=true"},
)

bool_flag(
    name = "disable_grpc_rls",
    build_setting_default = False,
)

bool_flag(
    name = "postmortem_checks",
    build_setting_default = False,
)

config_setting(
    name = "grpc_postmortem_checks_enabled",
    flag_values = {":postmortem_checks": "true"},
)

grpc_clang_cl_settings()

config_setting(
    name = "grpc_no_rls_flag",
    flag_values = {":disable_grpc_rls": "true"},
)

config_setting(
    name = "android",
    values = {"crosstool_top": "//external:android/crosstool"},
    # TODO: Use constraint_values to detect android after Bazel 7.0 platforms migration is finished
    # constraint_values = [ "@platforms//os:android" ],
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
    name = "visionos",
    values = {"apple_platform_type": "visionos"},
)

config_setting(
    name = "watchos",
    values = {"apple_platform_type": "watchos"},
)

config_setting(
    name = "windows_os",
    constraint_values = ["@platforms//os:windows"],
)

config_setting(
    name = "systemd",
    values = {"define": "use_systemd=true"},
)

config_setting(
    name = "fuchsia",
    constraint_values = ["@platforms//os:fuchsia"],
)

# Opt-ins for small clients, before the opt-out flag is applied.
selects.config_setting_group(
    name = "grpc_small_clients_enable",
    match_any = [
        ":small_client_flag",
        ":android",
        ":ios",
        ":fuchsia",
    ],
    visibility = ["//visibility:private"],
)

# Automatically disable certain deps for space-constrained clients where
# optional features may not be needed and binary size is more important.
# This includes Android, iOS, Fuchsia, and builds which request it explicitly with
# --//:small_client.
#
# A build can opt out of this behavior by setting --//:exclude_small_client.
selects.config_setting_group(
    name = "grpc_small_clients",
    match_all = [
        ":grpc_small_clients_enable",
        ":not_exclude_small_client_flag",
    ],
)

selects.config_setting_group(
    name = "grpc_no_xds",
    match_any = [
        ":grpc_no_xds_define",  # --define=grpc_no_xds=true
        ":grpc_small_clients",
    ],
)

selects.config_setting_group(
    name = "grpc_no_ztrace",
    match_any = [
        ":grpc_no_ztrace_define",  # --define=grpc_no_ztrace=true
        ":grpc_small_clients",
    ],
)

selects.config_setting_group(
    name = "grpc_no_rls",
    match_any = [
        ":grpc_no_rls_flag",  # --//:disable_grpc_rls
        ":grpc_small_clients",
    ],
)

selects.config_setting_group(
    name = "grpc_experiments_are_final",
    match_any = [
        ":grpc_experiments_are_final_define",  # --define=grpc_experiments_are_final=true
        ":grpc_small_clients",
    ],
)

bool_flag(
    name = "minimal_lb_policy",
    build_setting_default = False,
)

# Disable all load balancer policies except pick_first (the default).
# This saves binary size. However, this can be influenced by service config. So it should only be
# set by clients that know that none of their services are relying on load balancing. Thus this flag
# is not enabled by default even for grpc_small_clients.
config_setting(
    name = "grpc_minimal_lb_policy_flag",
    flag_values = {":minimal_lb_policy": "true"},
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
    constraint_values = ["@platforms//os:windows"],
)

config_setting(
    name = "mac",
    constraint_values = ["@platforms//os:macos"],
)

config_setting(
    name = "use_strict_warning",
    values = {"define": "use_strict_warning=true"},
)

config_setting(
    name = "use_strict_warning_windows",
    values = {"define": "use_strict_warning_windows=true"},
)

python_config_settings()

# This should be updated along with build_handwritten.yaml
g_stands_for = "glimmering"  # @unused

core_version = "52.0.0"  # @unused

version = "1.79.0-dev"  # @unused

GPR_PUBLIC_HDRS = [
    "include/grpc/support/alloc.h",
    "include/grpc/support/atm.h",
    "include/grpc/support/atm_gcc_atomic.h",
    "include/grpc/support/atm_gcc_sync.h",
    "include/grpc/support/atm_windows.h",
    "include/grpc/support/cpu.h",
    "include/grpc/support/json.h",
    "include/grpc/support/log.h",
    "include/grpc/support/log_windows.h",
    "include/grpc/support/metrics.h",
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
    "include/grpc/impl/codegen/gpr_types.h",
    "include/grpc/impl/codegen/log.h",
    "include/grpc/impl/codegen/sync.h",
    "include/grpc/impl/codegen/sync_abseil.h",
    "include/grpc/impl/codegen/sync_custom.h",
    "include/grpc/impl/codegen/sync_generic.h",
    "include/grpc/impl/codegen/sync_posix.h",
    "include/grpc/impl/codegen/sync_windows.h",
]

GRPC_PUBLIC_HDRS = [
    "include/grpc/grpc_audit_logging.h",
    "include/grpc/grpc_crl_provider.h",
    "include/grpc/byte_buffer.h",
    "include/grpc/byte_buffer_reader.h",
    "include/grpc/compression.h",
    "include/grpc/create_channel_from_endpoint.h",
    "include/grpc/fork.h",
    "include/grpc/grpc.h",
    "include/grpc/grpc_posix.h",
    "include/grpc/grpc_security.h",
    "include/grpc/grpc_security_constants.h",
    "include/grpc/passive_listener.h",
    "include/grpc/slice.h",
    "include/grpc/slice_buffer.h",
    "include/grpc/status.h",
    "include/grpc/load_reporting.h",
    "include/grpc/support/workaround_list.h",
    "include/grpc/impl/call.h",
    "include/grpc/impl/codegen/byte_buffer.h",
    "include/grpc/impl/codegen/byte_buffer_reader.h",
    "include/grpc/impl/codegen/compression_types.h",
    "include/grpc/impl/codegen/connectivity_state.h",
    "include/grpc/impl/codegen/fork.h",
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
    "include/grpc/event_engine/extensible.h",
    "include/grpc/event_engine/port.h",
    "include/grpc/event_engine/memory_allocator.h",
    "include/grpc/event_engine/memory_request.h",
    "include/grpc/event_engine/internal/memory_allocator_impl.h",
    "include/grpc/event_engine/slice.h",
    "include/grpc/event_engine/slice_buffer.h",
    "include/grpc/event_engine/internal/slice_cast.h",
    "include/grpc/event_engine/internal/write_event.h",
]

GRPCXX_SRCS = [
    "src/cpp/client/call_credentials.cc",
    "src/cpp/client/channel_cc.cc",
    "src/cpp/client/channel_credentials.cc",
    "src/cpp/client/client_callback.cc",
    "src/cpp/client/client_context.cc",
    "src/cpp/client/client_interceptor.cc",
    "src/cpp/client/client_stats_interceptor.cc",
    "src/cpp/client/create_channel.cc",
    "src/cpp/client/create_channel_internal.cc",
    "src/cpp/client/create_channel_posix.cc",
    "src/cpp/common/alarm.cc",
    "src/cpp/common/channel_arguments.cc",
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
    "src/cpp/server/server_credentials.cc",
    "src/cpp/server/server_posix.cc",
    "src/cpp/thread_manager/thread_manager.cc",
    "src/cpp/util/byte_buffer_cc.cc",
    "src/cpp/util/string_ref.cc",
    "src/cpp/util/time_cc.cc",
]

GRPCXX_HDRS = [
    "src/cpp/client/create_channel_internal.h",
    "src/cpp/client/client_stats_interceptor.h",
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
    "include/grpcpp/generic/callback_generic_service.h",
    "include/grpcpp/generic/generic_stub.h",
    "include/grpcpp/generic/generic_stub_callback.h",
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
    "include/grpcpp/impl/generic_stub_internal.h",
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
    "include/grpcpp/passive_listener.h",
    "include/grpcpp/resource_quota.h",
    "include/grpcpp/security/audit_logging.h",
    "include/grpcpp/security/tls_crl_provider.h",
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
    "include/grpcpp/ports_def.inc",
    "include/grpcpp/ports_undef.inc",
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
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time:time",
        "absl/functional:any_invocable",
    ],
    public_hdrs = GRPC_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_common",
        "grpc_core_credentials_header",
        "grpc_http_filters",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
        "iomgr_timer",
        "server",
        "transport_auth_context",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:client_channel_backup_poller",
        "//src/core:default_event_engine",
        "//src/core:endpoint_info_handshaker",
        "//src/core:experiments",
        "//src/core:fused_filters",
        "//src/core:grpc_authorization_base",
        "//src/core:http_proxy_mapper",
        "//src/core:init_internally",
        "//src/core:posix_event_engine_timer_manager",
        "//src/core:server_call_tracer_filter",
        "//src/core:service_config_channel_arg_filter",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:tcp_connect_handshaker",
        "//third_party/address_sorting",
    ],
)

GRPC_XDS_TARGETS = [
    "//src/core:grpc_lb_policy_cds",
    "//src/core:grpc_lb_policy_xds_cluster_impl",
    "//src/core:grpc_lb_policy_xds_cluster_manager",
    "//src/core:grpc_lb_policy_xds_override_host",
    "//src/core:grpc_lb_policy_xds_wrr_locality",
    "//src/core:grpc_resolver_xds",
    "//src/core:grpc_resolver_c2p",
    "//src/core:grpc_xds_server_config_fetcher",
    "//src/core:grpc_stateful_session_filter",
    "//src/core:xds_http_proxy_mapper",

    # Not xDS-specific but currently only used by xDS.
    "//src/core:channel_creds_registry_init",
    "//src/core:call_creds_registry_init",
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
    }) + select({
        # The registration is the only place where the plugins are referenced directly, so by
        # excluding them from BuildCoreConfiguration, they will be stripped by the linker.
        ":grpc_minimal_lb_policy_flag": ["GRPC_MINIMAL_LB_POLICY"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time:time",
        "absl/functional:any_invocable",
    ],
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
        "//visibility:public",
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
        "grpc_core_credentials_header",
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
        "server",
        "sockaddr_utils",
        "transport_auth_context",
        "tsi_base",
        "uri",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:channelz_v2tov1_legacy_api",
        "//src/core:client_channel_backup_poller",
        "//src/core:default_event_engine",
        "//src/core:endpoint_info_handshaker",
        "//src/core:experiments",
        "//src/core:fused_filters",
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
        "//src/core:http_proxy_mapper",
        "//src/core:httpcli_ssl_credentials",
        "//src/core:init_internally",
        "//src/core:json",
        "//src/core:posix_event_engine_timer_manager",
        "//src/core:ref_counted",
        "//src/core:server_call_tracer_filter",
        "//src/core:service_config_channel_arg_filter",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:sync",
        "//src/core:tcp_connect_handshaker",
        "//src/core:useful",
        "//third_party/address_sorting",
    ],
)

grpc_cc_library(
    name = "gpr",
    srcs = [
        "//src/core:util/alloc.cc",
        "//src/core:util/crash.cc",
        "//src/core:util/fork.cc",
        "//src/core:util/host_port.cc",
        "//src/core:util/iphone/cpu.cc",
        "//src/core:util/linux/cpu.cc",
        "//src/core:util/log.cc",
        "//src/core:util/mpscq.cc",
        "//src/core:util/msys/tmpfile.cc",
        "//src/core:util/posix/cpu.cc",
        "//src/core:util/posix/stat.cc",
        "//src/core:util/posix/string.cc",
        "//src/core:util/posix/thd.cc",
        "//src/core:util/posix/tmpfile.cc",
        "//src/core:util/string.cc",
        "//src/core:util/windows/cpu.cc",
        "//src/core:util/windows/stat.cc",
        "//src/core:util/windows/string.cc",
        "//src/core:util/windows/string_util.cc",
        "//src/core:util/windows/thd.cc",
        "//src/core:util/windows/tmpfile.cc",
    ],
    hdrs = [
        "//src/core:util/alloc.h",
        "//src/core:util/crash.h",
        "//src/core:util/fork.h",
        "//src/core:util/host_port.h",
        "//src/core:util/memory.h",
        "//src/core:util/mpscq.h",
        "//src/core:util/stat.h",
        "//src/core:util/string.h",
        "//src/core:util/thd.h",
        "//src/core:util/tmpfile.h",
        # TODO(ctiller): remove from gpr target entirely
        # All usage should be via gpr_platform
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/support/port_platform.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/base:log_severity",
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/log:globals",
        "absl/log:log",
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/time:time",
        "absl/functional:bind_front",
        "absl/flags:flag",
    ],
    public_hdrs = GPR_PUBLIC_HDRS,
    tags = [
        "nofixdeps",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "config_vars",
        "debug_location",
        "grpc_support_time",
        "//src/core:construct_destruct",
        "//src/core:env",
        "//src/core:event_engine_thread_local",
        "//src/core:examine_stack",
        "//src/core:gpr_atm",
        "//src/core:gpr_time",
        "//src/core:no_destruct",
        "//src/core:strerror",
        "//src/core:sync",
        "//src/core:tchar",
        "//src/core:time_precise",
        "//src/core:time_util",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "gpr_public_hdrs",
    hdrs = [
        # TODO(ctiller): remove from gpr target entirely
        # All usage should be via gpr_platform
        "include/grpc/impl/codegen/port_platform.h",
        "include/grpc/support/port_platform.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/base:core_headers",
    ],
    public_hdrs = GPR_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//bazel:gpr_public_hdrs"],
)

grpc_cc_library(
    name = "cpp_impl_of",
    hdrs = ["//src/core:util/cpp_impl_of.h"],
)

grpc_cc_library(
    name = "grpc_common",
    defines = select({
        "grpc_no_rls": ["GRPC_NO_RLS"],
        "//conditions:default": [],
    }),
    select_deps = [
        {
            "grpc_no_rls": [],
            "//conditions:default": ["//src/core:grpc_lb_policy_rls"],
        },
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_base",
        "add_port",
        # standard plugins
        "census",
        "//src/core:grpc_backend_metric_filter",
        "//src/core:grpc_client_authority_filter",
        "//src/core:grpc_lb_policy_grpclb",
        "//src/core:grpc_lb_policy_outlier_detection",
        "//src/core:grpc_lb_policy_pick_first",
        "//src/core:grpc_lb_policy_priority",
        "//src/core:grpc_lb_policy_ring_hash",
        "//src/core:grpc_lb_policy_round_robin",
        "//src/core:grpc_lb_policy_weighted_round_robin",
        "//src/core:grpc_lb_policy_weighted_target",
        "//src/core:grpc_channel_idle_filter",
        "//src/core:grpc_message_size_filter",
        "grpc_resolver_dns_ares",
        "grpc_resolver_fake",
        "//src/core:grpc_resolver_dns_native",
        "//src/core:grpc_resolver_sockaddr",
        "//src/core:grpc_transport_chttp2_client_connector",
        "//src/core:grpc_transport_chttp2_plugin",
        "//src/core:grpc_transport_chttp2_server",
        "//src/core:grpc_transport_inproc",
        "//src/core:grpc_fault_injection_filter",
        "//src/core:grpc_resolver_dns_plugin",
    ],
)

grpc_cc_library(
    name = "grpc_public_hdrs",
    hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
        "absl/utility",
        "absl/functional:any_invocable",
        "absl/status",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//bazel:grpc_public_hdrs"],
    deps = [
        "channel_arg_names",
        "gpr_public_hdrs",
        "grpc_core_credentials_header",
    ],
)

grpc_cc_library(
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
    external_deps = [
        "absl/log:log",
        "absl/log:absl_check",
        "absl/log:absl_log",
        "absl/status:statusor",
        "absl/strings:cord",
        "absl/synchronization",
        "protobuf_headers",
        "protobuf",
        "absl/base:core_headers",
        "absl/status",
        "absl/functional:any_invocable",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//bazel:grpc++_public_hdrs"],
    deps = [
        "global_callback_hook",
        "gpr",
        "grpc++_config_proto",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "//src/core:gpr_atm",
        "//src/core:grpc_check",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
    ],
)

grpc_cc_library(
    name = "channel_arg_names",
    hdrs = ["include/grpc/impl/channel_arg_names.h"],
)

grpc_cc_library(
    name = "grpc_slice",
    hdrs = [
        "include/grpc/impl/slice_type.h",
        "include/grpc/slice.h",
        "include/grpc/slice_buffer.h",
    ],
    external_deps = [
        "absl/base:core_headers",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "gpr",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc++",
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/log:absl_check",
        "absl/log:absl_log",
        "absl/strings:cord",
        "absl/base:core_headers",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization:synchronization",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/types:span",
    ],
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    select_deps = [
        {
            ":grpc_no_xds": [],
            "//conditions:default": [
                "grpc++_xds_client",
                "grpc++_xds_server",
            ],
        },
    ],
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
    deps = [
        "global_callback_hook",
        "gpr_public_hdrs",
        "grpc++_base",
        "grpc++_config_proto",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "ref_counted_ptr",
        "transport_auth_context",
        "//src/core:gpr_atm",
        "//src/core:grpc_check",
        "//src/core:slice",
        "@com_google_protobuf//:any_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
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
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "ref_counted_ptr",
        "//src/core:error",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_authorization_base",
        "//src/core:grpc_check",
        "//src/core:grpc_matchers",
        "//src/core:grpc_rbac_engine",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:load_file",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:sync",
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
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
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
        "absl/log:log",
        "absl/strings",
        "absl/types:span",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/message",
    ],
    deps = [
        "envoy_config_rbac_upb",
        "google_api_expr_v1alpha1_syntax_upb",
        "gpr",
        "grpc_mock_cel",
        "//src/core:grpc_authorization_base",
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
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "exec_ctx",
        "gpr",
        "grpc",
        "grpc++_base",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
        "transport_auth_context",
        "//src/core:grpc_check",
    ],
)

grpc_cc_library(
    name = "grpc++_xds_server",
    srcs = [
        "src/cpp/server/xds_server_builder.cc",
        "src/cpp/server/xds_server_credentials.cc",
    ],
    hdrs = [
        "src/cpp/server/secure_server_credentials.h",
    ],
    public_hdrs = [
        "include/grpcpp/xds_server_builder.h",
    ],
    visibility = ["//bazel:xds"],
    deps = [
        "channel_arg_names",
        "gpr",
        "grpc",
        "grpc++_base",
        "//src/core:grpc_check",
        "//src/core:xds_enabled_server",
    ],
)

# TODO(hork): restructure the grpc++_unsecure and grpc++ build targets in a
# similar way to how the grpc_unsecure and grpc targets were restructured in
# #25586
grpc_cc_library(
    name = "grpc++_unsecure",
    srcs = [
        "src/cpp/client/insecure_credentials.cc",
        "src/cpp/common/insecure_create_auth_context.cc",
        "src/cpp/server/insecure_server_credentials.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:log",
        "absl/log:absl_check",
        "absl/log:absl_log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
        "absl/synchronization",
    ],
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "channel_arg_names",
        "global_callback_hook",
        "gpr",
        "grpc++_base_unsecure",
        "grpc++_codegen_proto",
        "grpc++_config_proto",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_unsecure",
        "transport_auth_context",
        "//src/core:gpr_atm",
        "//src/core:grpc_check",
        "//src/core:grpc_insecure_credentials",
        "@com_google_protobuf//:any_cc_proto",
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
    standalone = True,
    visibility = ["//visibility:public"],
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
        "absl/log:log",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/message",
    ],
    standalone = True,
    visibility = ["//visibility:public"],
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
    public_hdrs = [
        "include/grpc/census.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "//src/core:arena",
    ],
)

# A library that vends only port_platform, so that libraries that don't need
# anything else from gpr can still be portable!
grpc_cc_library(
    name = "gpr_platform",
    external_deps = [
        "absl/base:core_headers",
        "absl/base:config",
        "absl/strings",
    ],
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
        "absl/types:span",
        "absl/functional:any_invocable",
        "absl/strings",
        "absl/utility:utility",
    ],
    tags = [
        "nofixdeps",
    ],
    visibility = ["//bazel:event_engine_base_hdrs"],
    deps = [
        "channel_arg_names",
        "gpr_platform",
        "gpr_public_hdrs",
        "grpc_core_credentials_header",
    ],
)

grpc_cc_library(
    name = "channelz",
    srcs = [
        "//src/core:channelz/channel_trace.cc",
        "//src/core:channelz/channelz.cc",
        "//src/core:channelz/channelz_registry.cc",
    ],
    hdrs = [
        "//src/core:channelz/channel_trace.h",
        "//src/core:channelz/channelz.h",
        "//src/core:channelz/channelz_registry.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:btree",
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
    ],
    visibility = ["//bazel:channelz"],
    deps = [
        "channelz_service_upb",
        "channelz_upb",
        "config_vars",
        "exec_ctx",
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
        "parse_address",
        "protobuf_any_upb",
        "ref_counted_ptr",
        "sockaddr_utils",
        "uri",
        "//src/core:channel_args",
        "//src/core:channelz_property_list",
        "//src/core:channelz_text_encode",
        "//src/core:connectivity_state",
        "//src/core:dual_ref_counted",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:memory_usage",
        "//src/core:notification",
        "//src/core:per_cpu",
        "//src/core:ref_counted",
        "//src/core:resolved_address",
        "//src/core:shared_bit_gen",
        "//src/core:single_set_ptr",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:time_precise",
        "//src/core:upb_utils",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "dynamic_annotations",
    hdrs = [
        "//src/core:lib/iomgr/dynamic_annotations.h",
    ],
    deps = [
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "call_combiner",
    srcs = [
        "//src/core:lib/iomgr/call_combiner.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/call_combiner.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/log:log",
    ],
    deps = [
        "dynamic_annotations",
        "exec_ctx",
        "gpr",
        "ref_counted_ptr",
        "stats",
        "//src/core:closure",
        "//src/core:gpr_atm",
        "//src/core:grpc_check",
        "//src/core:ref_counted",
        "//src/core:stats_data",
    ],
)

grpc_cc_library(
    name = "resource_quota_api",
    srcs = [
        "//src/core:lib/resource_quota/api.cc",
    ],
    hdrs = [
        "//src/core:lib/resource_quota/api.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel_arg_names",
        "config",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr_public_hdrs",
        "grpc_public_hdrs",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:thread_quota",
    ],
)

grpc_cc_library(
    name = "byte_buffer",
    srcs = [
        "//src/core:lib/surface/byte_buffer.cc",
        "//src/core:lib/surface/byte_buffer_reader.cc",
    ],
    deps = [
        "exec_ctx",
        "gpr_public_hdrs",
        "grpc_public_hdrs",
        "//src/core:compression",
        "//src/core:grpc_check",
        "//src/core:slice",
    ],
)

grpc_cc_library(
    name = "iomgr",
    srcs = [
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
        "//src/core:lib/iomgr/event_engine_shims/closure.cc",
        "//src/core:lib/iomgr/event_engine_shims/endpoint.cc",
        "//src/core:lib/iomgr/event_engine_shims/tcp_client.cc",
        "//src/core:lib/iomgr/fork_posix.cc",
        "//src/core:lib/iomgr/fork_windows.cc",
        "//src/core:lib/iomgr/iocp_windows.cc",
        "//src/core:lib/iomgr/iomgr.cc",
        "//src/core:lib/iomgr/iomgr_posix.cc",
        "//src/core:lib/iomgr/iomgr_posix_cfstream.cc",
        "//src/core:lib/iomgr/iomgr_windows.cc",
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
        "//src/core:util/gethostname_fallback.cc",
        "//src/core:util/gethostname_host_name_max.cc",
        "//src/core:util/gethostname_sysconf.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/block_annotate.h",
        "//src/core:lib/iomgr/cfstream_handle.h",
        "//src/core:lib/iomgr/endpoint.h",
        "//src/core:lib/iomgr/endpoint_cfstream.h",
        "//src/core:lib/iomgr/endpoint_pair.h",
        "//src/core:lib/iomgr/error_cfstream.h",
        "//src/core:lib/iomgr/ev_apple.h",
        "//src/core:lib/iomgr/ev_epoll1_linux.h",
        "//src/core:lib/iomgr/ev_poll_posix.h",
        "//src/core:lib/iomgr/ev_posix.h",
        "//src/core:lib/iomgr/iocp_windows.h",
        "//src/core:lib/iomgr/iomgr.h",
        "//src/core:lib/iomgr/lockfree_event.h",
        "//src/core:lib/iomgr/nameser.h",
        "//src/core:lib/iomgr/polling_entity.h",
        "//src/core:lib/iomgr/pollset.h",
        "//src/core:lib/iomgr/pollset_set_windows.h",
        "//src/core:lib/iomgr/pollset_windows.h",
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
        "//src/core:util/gethostname.h",
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
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:span",
        "absl/utility",
    ],
    linkopts = select({
        "systemd": ["-lsystemd"],
        "//conditions:default": [],
    }),
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel_arg_names",
        "config",
        "config_vars",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_trace",
        "iomgr_buffer_list",
        "iomgr_internal_errqueue",
        "iomgr_timer",
        "orphanable",
        "parse_address",
        "ref_counted_ptr",
        "resource_quota_api",
        "sockaddr_utils",
        "stats",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:closure",
        "//src/core:construct_destruct",
        "//src/core:context",
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:event_engine_common",
        "//src/core:event_engine_extensions",
        "//src/core:event_engine_memory_allocator_factory",
        "//src/core:event_engine_query_extensions",
        "//src/core:event_engine_shim",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:event_log",
        "//src/core:experiments",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_check",
        "//src/core:grpc_sockaddr",
        "//src/core:init_internally",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:memory_quota",
        "//src/core:no_destruct",
        "//src/core:pollset_set",
        "//src/core:posix_event_engine_base_hdrs",
        "//src/core:posix_event_engine_endpoint",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_cast",
        "//src/core:slice_refcount",
        "//src/core:socket_mutator",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:strerror",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:time_util",
        "//src/core:useful",
        "//src/core:windows_event_engine",
        "//src/core:windows_event_engine_listener",
    ],
)

grpc_cc_library(
    name = "call_tracer",
    srcs = [
        "//src/core:telemetry/call_tracer.cc",
    ],
    hdrs = [
        "//src/core:telemetry/call_tracer.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "//src/core:arena",
        "//src/core:call_final_info",
        "//src/core:channel_args",
        "//src/core:context",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:message",
        "//src/core:metadata_batch",
        "//src/core:ref_counted_string",
        "//src/core:slice_buffer",
        "//src/core:tcp_tracer",
    ],
)

grpc_cc_library(
    name = "channel",
    srcs = [
        "//src/core:lib/surface/channel.cc",
    ],
    hdrs = [
        "//src/core:lib/surface/channel.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel_arg_names",
        "channelz",
        "cpp_impl_of",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
        "ref_counted_ptr",
        "stats",
        "//src/core:arena",
        "//src/core:call_arena_allocator",
        "//src/core:call_destination",
        "//src/core:channel_args",
        "//src/core:channel_stack_type",
        "//src/core:compression",
        "//src/core:connectivity_state",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:iomgr_fwd",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:stats_data",
        "//src/core:sync",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "legacy_channel",
    srcs = [
        "//src/core:lib/surface/legacy_channel.cc",
    ],
    hdrs = [
        "//src/core:lib/surface/legacy_channel.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel",
        "channelz",
        "config",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "ref_counted_ptr",
        "stats",
        "//src/core:arena",
        "//src/core:blackboard",
        "//src/core:call_arena_allocator",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:dual_ref_counted",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:init_internally",
        "//src/core:iomgr_fwd",
        "//src/core:metrics",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:stats_data",
        "//src/core:sync",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "channel_create",
    srcs = [
        "//src/core:lib/surface/channel_create.cc",
    ],
    hdrs = [
        "//src/core:lib/surface/channel_create.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel",
        "channel_arg_names",
        "channelz",
        "config",
        "endpoint_addresses",
        "exec_ctx",
        "gpr_public_hdrs",
        "grpc_base",
        "grpc_client_channel",
        "grpc_security_base",
        "legacy_channel",
        "sockaddr_utils",
        "stats",
        "//:grpc_resolver_fake",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_stack_type",
        "//src/core:direct_channel",
        "//src/core:endpoint_channel_arg_wrapper",
        "//src/core:endpoint_transport",
        "//src/core:event_engine_common",
        "//src/core:event_engine_extensions",
        "//src/core:event_engine_query_extensions",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:stats_data",
    ],
)

grpc_cc_library(
    name = "server",
    srcs = [
        "//src/core:server/server.cc",
    ],
    hdrs = [
        "//src/core:server/server.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
        "absl/hash",
        "absl/log",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "call_combiner",
        "call_tracer",
        "channel",
        "channel_arg_names",
        "channelz",
        "config",
        "cpp_impl_of",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_trace",
        "iomgr",
        "legacy_channel",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "sockaddr_utils",
        "stats",
        "transport_auth_context",
        "//src/core:activity",
        "//src/core:arena_promise",
        "//src/core:blackboard",
        "//src/core:cancel_callback",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:channelz_property_list",
        "//src/core:closure",
        "//src/core:connection_quota",
        "//src/core:connectivity_state",
        "//src/core:context",
        "//src/core:dual_ref_counted",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:interception_chain",
        "//src/core:iomgr_fwd",
        "//src/core:map",
        "//src/core:metadata_batch",
        "//src/core:per_cpu",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:pollset_set",
        "//src/core:random_early_detection",
        "//src/core:resolved_address",
        "//src/core:seq",
        "//src/core:server_interface",
        "//src/core:shared_bit_gen",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_helper",
        "//src/core:stream_quota",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:try_join",
        "//src/core:try_seq",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "add_port",
    srcs = [
        "//src/core:server/add_port.cc",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "config",
        "exec_ctx",
        "grpc_security_base",
        "server",
        "//src/core:channel_args",
    ],
)

grpc_cc_library(
    name = "grpc_base",
    srcs = [
        "//src/core:call/client_call.cc",
        "//src/core:call/server_call.cc",
        "//src/core:call/status_util.cc",
        "//src/core:lib/channel/channel_stack.cc",
        "//src/core:lib/channel/channel_stack_builder_impl.cc",
        "//src/core:lib/channel/connected_channel.cc",
        "//src/core:lib/channel/promise_based_filter.cc",
        "//src/core:lib/compression/message_compress.cc",
        "//src/core:lib/surface/call.cc",
        "//src/core:lib/surface/call_details.cc",
        "//src/core:lib/surface/call_log_batch.cc",
        "//src/core:lib/surface/call_utils.cc",
        "//src/core:lib/surface/completion_queue.cc",
        "//src/core:lib/surface/completion_queue_factory.cc",
        "//src/core:lib/surface/event_string.cc",
        "//src/core:lib/surface/filter_stack_call.cc",
        "//src/core:lib/surface/lame_client.cc",
        "//src/core:lib/surface/metadata_array.cc",
        "//src/core:lib/surface/validate_metadata.cc",
        "//src/core:lib/surface/version.cc",
        "//src/core:lib/transport/transport.cc",
        "//src/core:lib/transport/transport_op_string.cc",
    ],
    hdrs = [
        "//src/core:call/client_call.h",
        "//src/core:call/server_call.h",
        "//src/core:call/status_util.h",
        "//src/core:lib/channel/channel_stack.h",
        "//src/core:lib/channel/channel_stack_builder_impl.h",
        "//src/core:lib/channel/connected_channel.h",
        "//src/core:lib/channel/promise_based_filter.h",
        "//src/core:lib/compression/message_compress.h",
        "//src/core:lib/surface/call.h",
        "//src/core:lib/surface/call_test_only.h",
        "//src/core:lib/surface/call_utils.h",
        "//src/core:lib/surface/completion_queue.h",
        "//src/core:lib/surface/completion_queue_factory.h",
        "//src/core:lib/surface/event_string.h",
        "//src/core:lib/surface/filter_stack_call.h",
        "//src/core:lib/surface/init.h",
        "//src/core:lib/surface/lame_client.h",
        "//src/core:lib/surface/validate_metadata.h",
        "//src/core:lib/transport/transport.h",
    ],
    defines = select({
        "systemd": ["HAVE_LIBSYSTEMD"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/log",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:span",
        "absl/utility",
        "madler_zlib",
        "@com_google_protobuf//upb/mem",
    ],
    linkopts = select({
        "systemd": ["-lsystemd"],
        "//conditions:default": [],
    }),
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "byte_buffer",
        "call_combiner",
        "call_tracer",
        "channel",
        "channel_arg_names",
        "channel_stack_builder",
        "channelz",
        "config",
        "config_vars",
        "cpp_impl_of",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_trace",
        "iomgr",
        "iomgr_timer",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "stats",
        "//src/core:1999",
        "//src/core:activity",
        "//src/core:all_ok",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:atomic_utils",
        "//src/core:bitset",
        "//src/core:blackboard",
        "//src/core:call_destination",
        "//src/core:call_filters",
        "//src/core:call_final_info",
        "//src/core:call_finalization",
        "//src/core:call_spine",
        "//src/core:cancel_callback",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:channelz_property_list",
        "//src/core:closure",
        "//src/core:compression",
        "//src/core:connectivity_state",
        "//src/core:context",
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:event_engine_common",
        "//src/core:event_engine_context",
        "//src/core:event_engine_shim",
        "//src/core:experiments",
        "//src/core:filter_args",
        "//src/core:for_each",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:gpr_spinlock",
        "//src/core:grpc_check",
        "//src/core:http2_status",
        "//src/core:if",
        "//src/core:iomgr_fwd",
        "//src/core:latch",
        "//src/core:latent_see",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:match",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:metrics",
        "//src/core:no_destruct",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:promise_like",
        "//src/core:promise_status",
        "//src/core:race",
        "//src/core:ref_counted",
        "//src/core:seq",
        "//src/core:server_interface",
        "//src/core:single_set_ptr",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_cast",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_flag",
        "//src/core:status_helper",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:time_precise",
        "//src/core:transport_fwd",
        "//src/core:try_seq",
        "//src/core:type_list",
        "//src/core:unique_type_name",
        "//src/core:upb_utils",
        "//src/core:useful",
        "//src/proto/grpc/channelz/v2:promise_upb_proto",
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
    external_deps = [
        "absl/log:log",
    ],
    deps = [
        "gpr",
        "gpr_platform",
        "grpc++",
        "//src/core:grpc_check",
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
    tags = [
        "grpc:broken-internally",
    ],
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
    external_deps = [
        "absl/log:log",
    ],
    public_hdrs = [
        "include/grpcpp/ext/server_load_reporting.h",
    ],
    tags = [
        "nofixdeps",
        # uses OSS specific libraries
        "grpc:broken-internally",
    ],
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
        "absl/log:log",
        "absl/memory",
        "protobuf_headers",
    ],
    tags = [
        "grpc:broken-internally",
        "nofixdeps",
    ],
    deps = [
        ":gpr",
        ":grpc++",
        ":lb_load_reporter",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//src/proto/grpc/lb/v1:load_reporter_cc_grpc",
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
    external_deps = [
        "absl/log:log",
    ],
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
        "absl/log:log",
        "opencensus-stats",
        "opencensus-tags",
        "protobuf_headers",
    ],
    tags = [
        "grpc:broken-internally",
        "nofixdeps",
    ],
    deps = [
        "gpr",
        "lb_get_cpu_stats",
        "lb_load_data_store",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//src/proto/grpc/lb/v1:load_reporter_cc_grpc",
    ],
)

grpc_cc_library(
    name = "transport_auth_context",
    srcs = [
        "//src/core:transport/auth_context.cc",
    ],
    hdrs = [
        "//src/core:transport/auth_context.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/strings",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_trace",
        "orphanable",
        "ref_counted_ptr",
        "resource_quota_api",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:connection_context",
        "//src/core:grpc_check",
        "//src/core:ref_counted",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_security_base",
    srcs = [
        "//src/core:call/security_context.cc",
        "//src/core:credentials/call/call_creds_util.cc",
        "//src/core:credentials/call/composite/composite_call_credentials.cc",
        "//src/core:credentials/call/plugin/plugin_credentials.cc",
        "//src/core:credentials/transport/composite/composite_channel_credentials.cc",
        "//src/core:credentials/transport/security_connector.cc",
        "//src/core:credentials/transport/transport_credentials.cc",
        "//src/core:filter/auth/client_auth_filter.cc",
        "//src/core:filter/auth/server_auth_filter.cc",
        "//src/core:handshaker/security/legacy_secure_endpoint.cc",
        "//src/core:handshaker/security/pipelined_secure_endpoint.cc",
        "//src/core:handshaker/security/secure_endpoint.cc",
        "//src/core:handshaker/security/security_handshaker.cc",
    ],
    hdrs = [
        "//src/core:call/security_context.h",
        "//src/core:credentials/call/call_credentials.h",
        "//src/core:credentials/call/call_creds_util.h",
        "//src/core:credentials/call/composite/composite_call_credentials.h",
        "//src/core:credentials/call/plugin/plugin_credentials.h",
        "//src/core:credentials/transport/composite/composite_channel_credentials.h",
        "//src/core:credentials/transport/security_connector.h",
        "//src/core:credentials/transport/transport_credentials.h",
        "//src/core:filter/auth/auth_filters.h",
        "//src/core:handshaker/security/secure_endpoint.h",
        "//src/core:handshaker/security/security_handshaker.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["//visibility:public"],
    deps = [
        "channel_arg_names",
        "channelz",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_trace",
        "handshaker",
        "iomgr",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "resource_quota_api",
        "stats",
        "transport_auth_context",
        "tsi_base",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:closure",
        "//src/core:connection_context",
        "//src/core:context",
        "//src/core:error",
        "//src/core:event_engine_memory_allocator",
        "//src/core:experiments",
        "//src/core:gpr_atm",
        "//src/core:grpc_check",
        "//src/core:handshaker_factory",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:latent_see",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:poll",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:sync",
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
    tags = ["nofixdeps"],
    visibility = ["//bazel:tsi_interface"],
    deps = [
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "grpc_security_constants",
    hdrs = [
        "include/grpc/grpc_security_constants.h",
    ],
    visibility = ["//:__subpackages__"],
)

grpc_cc_library(
    name = "grpc_status",
    hdrs = [
        "include/grpc/status.h",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "compression_types",
    hdrs = [
        "include/grpc/impl/compression_types.h",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_support_time",
    hdrs = [
        "include/grpc/support/time.h",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_types",
    hdrs = [
        "include/grpc/impl/grpc_types.h",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "channel_arg_names",
        "compression_types",
        "gpr_platform",
        "grpc_slice",
        "grpc_status",
        "grpc_support_time",
    ],
)

grpc_cc_library(
    name = "grpc_event_engine_slice",
    hdrs = [
        "include/grpc/event_engine/slice.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    public_hdrs = [
        "include/grpc/event_engine/internal/slice_cast.h",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "gpr_platform",
        "grpc_slice",
        "grpc_types",
    ],
)

# TODO(hork): split credentials types into their own source files and targets.
grpc_cc_library(
    name = "grpc_core_credentials_header",
    hdrs = [
        "include/grpc/credentials.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/utility",
    ],
    visibility = ["//bazel:core_credentials"],
    deps = [
        "gpr_public_hdrs",
        "grpc_security_constants",
        "grpc_slice",
        "grpc_status",
        "grpc_types",
    ],
)

grpc_cc_library(
    name = "alts_util",
    srcs = [
        "//src/core:credentials/transport/alts/check_gcp_environment.cc",
        "//src/core:credentials/transport/alts/check_gcp_environment_linux.cc",
        "//src/core:credentials/transport/alts/check_gcp_environment_no_op.cc",
        "//src/core:credentials/transport/alts/check_gcp_environment_windows.cc",
        "//src/core:credentials/transport/alts/grpc_alts_credentials_client_options.cc",
        "//src/core:credentials/transport/alts/grpc_alts_credentials_options.cc",
        "//src/core:credentials/transport/alts/grpc_alts_credentials_server_options.cc",
        "//src/core:tsi/alts/handshaker/transport_security_common_api.cc",
    ],
    hdrs = [
        "include/grpc/grpc_security.h",
        "//src/core:credentials/transport/alts/check_gcp_environment.h",
        "//src/core:credentials/transport/alts/grpc_alts_credentials_options.h",
        "//src/core:tsi/alts/handshaker/transport_security_common_api.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/status:statusor",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
    ],
    visibility = ["//bazel:tsi"],
    deps = [
        "alts_upb",
        "gpr",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "tsi",
    external_deps = [
        "libssl",
        "libcrypto",
        "absl/strings",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:tsi"],
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
        "absl/functional:any_invocable",
        "absl/log:log",
        "absl/log:absl_check",
        "absl/log:absl_log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/memory",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "protobuf_headers",
        "absl/container:inlined_vector",
    ],
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = ["nofixdeps"],
    visibility = ["//bazel:alt_grpc++_base_legacy"],
    deps = [
        "channel",
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "global_callback_hook",
        "gpr",
        "grpc",
        "grpc++_codegen_proto",
        "grpc++_config_proto",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_credentials_util",
        "grpc_health_upb",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpcpp_backend_metric_recorder",
        "grpcpp_call_metric_recorder",
        "grpcpp_status",
        "iomgr",
        "iomgr_timer",
        "ref_counted_ptr",
        "resource_quota_api",
        "server",
        "transport_auth_context",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_backend_metric_provider",
        "//src/core:grpc_check",
        "//src/core:grpc_crl_provider",
        "//src/core:grpc_service_config",
        "//src/core:grpc_tls_credentials",
        "//src/core:grpc_transport_chttp2_server",
        "//src/core:grpc_transport_inproc",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:load_file",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:socket_mutator",
        "//src/core:status_helper",
        "//src/core:sync",
        "//src/core:thread_quota",
        "//src/core:time",
        "//src/core:useful",
        "@com_google_protobuf//:any_cc_proto",
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
        "absl/functional:any_invocable",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
        "absl/synchronization",
        "absl/log:absl_check",
        "absl/log:absl_log",
        "absl/memory",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "absl/strings:str_format",
        "protobuf_headers",
    ],
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["//bazel:alt_grpc++_base_unsecure_legacy"],
    deps = [
        "channel",
        "channel_arg_names",
        "channel_stack_builder",
        "config",
        "exec_ctx",
        "global_callback_hook",
        "gpr",
        "grpc++_config_proto",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_health_upb",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_transport_chttp2",
        "grpc_unsecure",
        "grpcpp_backend_metric_recorder",
        "grpcpp_call_metric_recorder",
        "grpcpp_status",
        "iomgr",
        "iomgr_timer",
        "ref_counted_ptr",
        "resource_quota_api",
        "server",
        "transport_auth_context",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channel_init",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:gpr_atm",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_backend_metric_provider",
        "//src/core:grpc_check",
        "//src/core:grpc_insecure_credentials",
        "//src/core:grpc_service_config",
        "//src/core:grpc_transport_chttp2_server",
        "//src/core:grpc_transport_inproc",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:socket_mutator",
        "//src/core:sync",
        "//src/core:thread_quota",
        "//src/core:time",
        "//src/core:useful",
        "@com_google_protobuf//:any_cc_proto",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_proto",
    hdrs = ["include/grpcpp/impl/generic_serialize.h"],
    external_deps = [
        "absl/strings:cord",
        "protobuf_headers",
        "protobuf",
        "absl/log:check",
        "absl/log:absl_check",
        "absl/strings",
    ],
    public_hdrs = [
        "include/grpc++/impl/codegen/proto_utils.h",
        "include/grpcpp/impl/codegen/proto_buffer_reader.h",
        "include/grpcpp/impl/codegen/proto_buffer_writer.h",
        "include/grpcpp/impl/codegen/proto_utils.h",
        "include/grpcpp/impl/proto_utils.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
    deps = [
        "grpc++_config_proto",
        "grpc++_public_hdrs",
        "grpc_base",
        "grpcpp_status",
    ],
)

grpc_cc_library(
    name = "grpc++_config_proto",
    external_deps = [
        "absl/status",
        "protobuf_headers",
        "protobuf",
    ],
    public_hdrs = [
        "include/grpc++/impl/codegen/config_protobuf.h",
        "include/grpcpp/impl/codegen/config_protobuf.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
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
    external_deps = [
        "protobuf_headers",
    ],
    public_hdrs = [
        "include/grpc++/ext/proto_server_reflection_plugin.h",
        "include/grpcpp/ext/proto_server_reflection_plugin.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["//visibility:public"],
    deps = [
        "config_vars",
        "grpc++",
        "grpc++_config_proto",
        "//src/proto/grpc/reflection/v1:reflection_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_call_metric_recorder",
    external_deps = [
        "absl/strings",
    ],
    public_hdrs = [
        "include/grpcpp/ext/call_metric_recorder.h",
    ],
    visibility = ["//visibility:public"],
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
        "absl/log:log",
        "absl/strings",
    ],
    public_hdrs = [
        "include/grpcpp/ext/server_metric_recorder.h",
    ],
    visibility = ["//visibility:public"],
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
    hdrs = [
        "src/cpp/server/orca/orca_service.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/strings",
        "absl/time",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
    ],
    public_hdrs = [
        "include/grpcpp/ext/orca_service.h",
    ],
    visibility = ["//visibility:public"],
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
        "//src/core:grpc_check",
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
        "absl/log",
        "absl/strings",
        "protobuf_headers",
    ],
    public_hdrs = [
        "include/grpcpp/ext/channelz_service_plugin.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:channelz"],
    deps = [
        "channelz",
        "gpr",
        "grpc",
        "grpc++",
        "grpc++_config_proto",
        "//src/core:channelz_v2tov1_convert",
        "//src/core:experiments",
        "//src/core:notification",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/channelz/v2:service_cc_grpc",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_latent_see_service",
    srcs = [
        "src/cpp/latent_see/latent_see_service.cc",
    ],
    hdrs = [
        "src/cpp/latent_see/latent_see_service.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb/mem",
        "protobuf_headers",
        "absl/log",
        "absl/strings:string_view",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:latent_see"],
    deps = [
        "gpr",
        "grpc",
        "grpc++",
        "grpc++_config_proto",
        "//src/core:channelz_property_list",
        "//src/core:latent_see",
        "//src/proto/grpc/channelz/v2:latent_see_cc_grpc",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_upb_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_latent_see_client",
    srcs = [
        "src/cpp/latent_see/latent_see_client.cc",
    ],
    hdrs = [
        "src/cpp/latent_see/latent_see_client.h",
    ],
    external_deps = [
        "protobuf_headers",
        "absl/log",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:latent_see"],
    deps = [
        "gpr",
        "grpc",
        "grpc++",
        "grpc++_config_proto",
        "//src/core:channelz_property_list",
        "//src/core:latent_see",
        "//src/core:time",
        "//src/proto/grpc/channelz/v2:latent_see_cc_grpc",
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
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "grpc",
        "grpc++_base",
        "@envoy_api//envoy/service/status/v3:pkg_cc_grpc",
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
    visibility = ["//visibility:public"],
    deps = [
        "channel",
        "grpc++",
        "grpc_base",
    ],
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
        "absl/base:core_headers",
        "absl/numeric:bits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "opencensus-stats",
        "opencensus-tags",
        "opencensus-tags-context_util",
        "opencensus-trace",
        "opencensus-trace-context_util",
        "opencensus-trace-propagation",
        "opencensus-trace-span_context",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "call_tracer",
        "config",
        "gpr",
        "grpc++_base",
        "grpc_base",
        "grpc_public_hdrs",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:context",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:logging_filter",
        "//src/core:metadata_batch",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:sync",
        "//src/core:tcp_tracer",
    ],
)

# This is an EXPERIMENTAL target subject to change.
grpc_cc_library(
    name = "grpcpp_gcp_observability",
    hdrs = [
        "include/grpcpp/ext/gcp_observability.h",
    ],
    tags = [
        # This can be removed once we can add top-level BUILD file targets without them being
        # included in Core Components.
        "grpc:broken-internally",
        "nofixdeps",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//src/cpp/ext/gcp:observability",
    ],
)

# This is an EXPERIMENTAL target subject to change.
grpc_cc_library(
    name = "grpcpp_csm_observability",
    hdrs = [
        "include/grpcpp/ext/csm_observability.h",
    ],
    tags = [
        # This can be removed once we can add top-level BUILD file targets without them being
        # included in Core Components.
        "grpc:broken-internally",
        "nofixdeps",
    ],
    deps = [
        ":grpcpp_otel_plugin",
        "//src/cpp/ext/csm:csm_observability",
    ],
)

# This is an EXPERIMENTAL target subject to change.
grpc_cc_library(
    name = "grpcpp_otel_plugin",
    hdrs = [
        "include/grpcpp/ext/otel_plugin.h",
    ],
    tags = [
        # This can be removed once we can add top-level BUILD file targets without them being
        # included in Core Components.
        "grpc:broken-internally",
    ],
    deps = [
        ":grpc++",
        "//src/cpp/ext/otel:otel_plugin",
    ],
)

grpc_cc_library(
    name = "generic_stub_internal",
    hdrs = [
        "include/grpcpp/impl/generic_stub_internal.h",
    ],
    deps = [
        "grpc++_public_hdrs",
    ],
)

grpc_cc_library(
    name = "generic_stub",
    hdrs = [
        "include/grpcpp/generic/generic_stub.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "generic_stub_internal",
        "grpc++_base",
    ],
)

grpc_cc_library(
    name = "generic_stub_callback",
    hdrs = [
        "include/grpcpp/generic/generic_stub_callback.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "generic_stub_internal",
        "grpc++",
    ],
)

grpc_cc_library(
    name = "async_generic_service",
    hdrs = [
        "include/grpcpp/generic/async_generic_service.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "gpr_platform",
        "grpc++_public_hdrs",
    ],
)

grpc_cc_library(
    name = "callback_generic_service",
    hdrs = [
        "include/grpcpp/generic/callback_generic_service.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "grpc++_public_hdrs",
    ],
)

grpc_cc_library(
    name = "work_serializer",
    srcs = [
        "//src/core:util/work_serializer.cc",
    ],
    hdrs = [
        "//src/core:util/work_serializer.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/log",
        "absl/log:check",
        "absl/functional:any_invocable",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "debug_location",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "grpc_trace",
        "orphanable",
        "stats",
        "//src/core:experiments",
        "//src/core:latent_see",
        "//src/core:stats_data",
        "//src/core:sync",
    ],
)

grpc_cc_library(
    name = "grpc_trace",
    srcs = [
        "//src/core:lib/debug/trace.cc",
        "//src/core:lib/debug/trace_flags.cc",
    ],
    hdrs = [
        "//src/core:lib/debug/trace.h",
        "//src/core:lib/debug/trace_flags.h",
        "//src/core:lib/debug/trace_impl.h",
    ],
    external_deps = [
        "absl/log",
        "absl/strings",
        "absl/container:flat_hash_map",
    ],
    visibility = ["//bazel:trace"],
    deps = [
        "config_vars",
        "gpr",
        "//src/core:glob",
        "//src/core:no_destruct",
    ],
)

grpc_filegroup(
    name = "trace_flag_files",
    srcs = ["//src/core:lib/debug/trace_flags.yaml"],
)

grpc_cc_library(
    name = "load_config",
    srcs = [
        "//src/core:config/load_config.cc",
    ],
    hdrs = [
        "//src/core:config/load_config.h",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:marshalling",
        "absl/log:check",
        "absl/strings",
    ],
    deps = [
        "gpr_platform",
        "//src/core:env",
    ],
)

grpc_cc_library(
    name = "config_vars",
    srcs = [
        "//src/core:config/config_vars.cc",
        "//src/core:config/config_vars_non_generated.cc",
    ],
    hdrs = [
        "//src/core:config/config_vars.h",
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
        "//src/core:config/core_configuration.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
    ],
    public_hdrs = [
        "//src/core:config/core_configuration.h",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "debug_location",
        "gpr",
        "grpc_resolver",
        "//src/core:auth_context_comparator_registry",
        "//src/core:call_creds_registry",
        "//src/core:certificate_provider_registry",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_creds_registry",
        "//src/core:channel_init",
        "//src/core:endpoint_transport",
        "//src/core:grpc_check",
        "//src/core:handshaker_registry",
        "//src/core:lb_policy_registry",
        "//src/core:proxy_mapper_registry",
        "//src/core:service_config_parser",
    ],
)

grpc_cc_library(
    name = "debug_location",
    external_deps = ["absl/strings"],
    public_hdrs = ["//src/core:util/debug_location.h"],
    visibility = ["//bazel:debug_location"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "orphanable",
    public_hdrs = ["//src/core:util/orphanable.h"],
    visibility = [
        "//bazel:client_channel",
        "//bazel:xds_client_core",
    ],
    deps = [
        "debug_location",
        "gpr_platform",
        "ref_counted_ptr",
        "//src/core:down_cast",
        "//src/core:ref_counted",
    ],
)

grpc_cc_library(
    name = "promise",
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
    ],
    public_hdrs = [
        "//src/core:lib/promise/promise.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "//src/core:poll",
        "//src/core:promise_like",
    ],
)

grpc_cc_library(
    name = "ref_counted_ptr",
    external_deps = ["absl/hash"],
    public_hdrs = ["//src/core:util/ref_counted_ptr.h"],
    visibility = ["//bazel:ref_counted_ptr"],
    deps = [
        "debug_location",
        "gpr_platform",
        "//src/core:down_cast",
    ],
)

grpc_cc_library(
    name = "handshaker",
    srcs = [
        "//src/core:handshaker/handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
    ],
    public_hdrs = [
        "//src/core:handshaker/handshaker.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channelz",
        "debug_location",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "iomgr",
        "orphanable",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_helper",
        "//src/core:sync",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "http_connect_handshaker",
    srcs = [
        "//src/core:handshaker/http_connect/http_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/status",
        "absl/strings",
    ],
    public_hdrs = [
        "//src/core:handshaker/http_connect/http_connect_handshaker.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "handshaker",
        "httpcli",
        "iomgr",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:handshaker_factory",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:sync",
    ],
)

grpc_cc_library(
    name = "exec_ctx",
    srcs = [
        "//src/core:lib/iomgr/combiner.cc",
        "//src/core:lib/iomgr/exec_ctx.cc",
        "//src/core:lib/iomgr/iomgr_internal.cc",
    ],
    hdrs = [
        "//src/core:lib/iomgr/combiner.h",
        "//src/core:lib/iomgr/exec_ctx.h",
        "//src/core:lib/iomgr/iomgr_internal.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings:str_format",
    ],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
        "//bazel:exec_ctx",
    ],
    deps = [
        "debug_location",
        "gpr",
        "grpc_public_hdrs",
        "grpc_trace",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:gpr_atm",
        "//src/core:gpr_spinlock",
        "//src/core:latent_see",
        "//src/core:time",
        "//src/core:time_precise",
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
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "uri",
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
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    deps = [
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "gpr_platform",
        "grpc_trace",
        "//src/core:closure",
        "//src/core:gpr_manual_constructor",
        "//src/core:gpr_spinlock",
        "//src/core:grpc_check",
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
    external_deps = [
        "absl/log:log",
    ],
    tags = ["nofixdeps"],
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
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "iomgr_internal_errqueue",
        "//src/core:error",
        "//src/core:iomgr_port",
        "//src/core:sync",
    ],
)

grpc_cc_library(
    name = "uri",
    srcs = [
        "//src/core:util/uri.cc",
    ],
    hdrs = [
        "//src/core:util/uri.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
        "//bazel:client_channel",
    ],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "parse_address",
    srcs = [
        "//src/core:lib/address_utils/parse_address.cc",
        "//src/core:util/grpc_if_nametoindex_posix.cc",
        "//src/core:util/grpc_if_nametoindex_unsupported.cc",
    ],
    hdrs = [
        "//src/core:lib/address_utils/parse_address.h",
        "//src/core:util/grpc_if_nametoindex.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "uri",
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
        "//src/core:util/backoff.cc",
    ],
    hdrs = [
        "//src/core:util/backoff.h",
    ],
    external_deps = ["absl/random"],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "//src/core:experiments",
        "//src/core:shared_bit_gen",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "stats",
    srcs = [
        "//src/core:telemetry/stats.cc",
    ],
    hdrs = [
        "//src/core:telemetry/stats.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:span",
    ],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
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
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "gpr",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:filter_args",
    ],
)

grpc_cc_library(
    name = "grpc_service_config_impl",
    srcs = [
        "//src/core:service_config/service_config_impl.cc",
    ],
    hdrs = [
        "//src/core:service_config/service_config_impl.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "config",
        "gpr",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_check",
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
    name = "endpoint_addresses",
    srcs = [
        "//src/core:resolver/endpoint_addresses.cc",
    ],
    hdrs = [
        "//src/core:resolver/endpoint_addresses.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "gpr",
        "gpr_platform",
        "sockaddr_utils",
        "//src/core:channel_args",
        "//src/core:grpc_check",
        "//src/core:resolved_address",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "server_address",
    hdrs = [
        "//src/core:resolver/server_address.h",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "endpoint_addresses",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "grpc_resolver",
    srcs = [
        "//src/core:resolver/resolver.cc",
        "//src/core:resolver/resolver_registry.cc",
    ],
    hdrs = [
        "//src/core:resolver/resolver.h",
        "//src/core:resolver/resolver_factory.h",
        "//src/core:resolver/resolver_registry.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "endpoint_addresses",
        "gpr",
        "grpc_trace",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "uri",
        "//src/core:channel_args",
        "//src/core:grpc_check",
        "//src/core:grpc_service_config",
        "//src/core:iomgr_fwd",
    ],
)

grpc_cc_library(
    name = "oob_backend_metric",
    srcs = [
        "//src/core:load_balancing/oob_backend_metric.cc",
    ],
    hdrs = [
        "//src/core:load_balancing/oob_backend_metric.h",
        "//src/core:load_balancing/oob_backend_metric_internal.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb/mem",
        "absl/base:core_headers",
        "absl/log",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "channelz",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_client_channel",
        "grpc_public_hdrs",
        "grpc_trace",
        "orphanable",
        "protobuf_duration_upb",
        "ref_counted_ptr",
        "xds_orca_service_upb",
        "//src/core:backend_metric_parser",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_check",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:slice",
        "//src/core:subchannel_interface",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:unique_type_name",
    ],
)

grpc_cc_library(
    name = "lb_child_policy_handler",
    srcs = [
        "//src/core:load_balancing/child_policy_handler.cc",
    ],
    hdrs = [
        "//src/core:load_balancing/child_policy_handler.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "config",
        "debug_location",
        "gpr_public_hdrs",
        "grpc_public_hdrs",
        "grpc_trace",
        "orphanable",
        "ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:connectivity_state",
        "//src/core:delegating_helper",
        "//src/core:grpc_check",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:subchannel_interface",
    ],
)

grpc_cc_library(
    name = "grpc_client_channel",
    srcs = [
        "//src/core:client_channel/buffered_call.cc",
        "//src/core:client_channel/client_channel.cc",
        "//src/core:client_channel/client_channel_factory.cc",
        "//src/core:client_channel/client_channel_filter.cc",
        "//src/core:client_channel/client_channel_plugin.cc",
        "//src/core:client_channel/dynamic_filters.cc",
        "//src/core:client_channel/global_subchannel_pool.cc",
        "//src/core:client_channel/load_balanced_call_destination.cc",
        "//src/core:client_channel/local_subchannel_pool.cc",
        "//src/core:client_channel/retry_filter.cc",
        "//src/core:client_channel/retry_filter_legacy_call_data.cc",
        "//src/core:client_channel/subchannel.cc",
        "//src/core:client_channel/subchannel_stream_client.cc",
    ],
    hdrs = [
        "//src/core:client_channel/buffered_call.h",
        "//src/core:client_channel/client_channel.h",
        "//src/core:client_channel/client_channel_factory.h",
        "//src/core:client_channel/client_channel_filter.h",
        "//src/core:client_channel/dynamic_filters.h",
        "//src/core:client_channel/global_subchannel_pool.h",
        "//src/core:client_channel/load_balanced_call_destination.h",
        "//src/core:client_channel/local_subchannel_pool.h",
        "//src/core:client_channel/retry_filter.h",
        "//src/core:client_channel/retry_filter_legacy_call_data.h",
        "//src/core:client_channel/subchannel.h",
        "//src/core:client_channel/subchannel_interface_internal.h",
        "//src/core:client_channel/subchannel_stream_client.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
    ],
    visibility = ["//bazel:client_channel"],
    deps = [
        "backoff",
        "call_combiner",
        "call_tracer",
        "channel",
        "channel_arg_names",
        "channelz",
        "config",
        "debug_location",
        "endpoint_addresses",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "iomgr",
        "lb_child_policy_handler",
        "orphanable",
        "parse_address",
        "promise",
        "ref_counted_ptr",
        "sockaddr_utils",
        "stats",
        "transport_auth_context",
        "uri",
        "work_serializer",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:backend_metric_parser",
        "//src/core:blackboard",
        "//src/core:call_destination",
        "//src/core:call_spine",
        "//src/core:cancel_callback",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_fwd",
        "//src/core:channel_init",
        "//src/core:channel_stack_type",
        "//src/core:client_channel_args",
        "//src/core:client_channel_backup_poller",
        "//src/core:client_channel_internal_header",
        "//src/core:client_channel_service_config",
        "//src/core:closure",
        "//src/core:config_selector",
        "//src/core:connectivity_state",
        "//src/core:construct_destruct",
        "//src/core:context",
        "//src/core:dual_ref_counted",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:exec_ctx_wakeup_scheduler",
        "//src/core:experiments",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_channel_idle_filter",
        "//src/core:grpc_check",
        "//src/core:grpc_service_config",
        "//src/core:idle_filter_state",
        "//src/core:init_internally",
        "//src/core:interception_chain",
        "//src/core:iomgr_fwd",
        "//src/core:json",
        "//src/core:latch",
        "//src/core:lb_metadata",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:memory_quota",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:metrics",
        "//src/core:observable",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:pollset_set",
        "//src/core:proxy_mapper_registry",
        "//src/core:ref_counted",
        "//src/core:resolved_address",
        "//src/core:resource_quota",
        "//src/core:retry_interceptor",
        "//src/core:retry_service_config",
        "//src/core:retry_throttle",
        "//src/core:seq",
        "//src/core:single_set_ptr",
        "//src/core:sleep",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_helper",
        "//src/core:subchannel_connector",
        "//src/core:subchannel_interface",
        "//src/core:subchannel_pool_interface",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:time_precise",
        "//src/core:try_seq",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "//src/core:resolver/dns/c_ares/dns_resolver_ares.cc",
        "//src/core:resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "//src/core:resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc",
        "//src/core:resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "//src/core:resolver/dns/c_ares/grpc_ares_wrapper_posix.cc",
        "//src/core:resolver/dns/c_ares/grpc_ares_wrapper_windows.cc",
    ],
    hdrs = [
        "//src/core:resolver/dns/c_ares/dns_resolver_ares.h",
        "//src/core:resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "//src/core:resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "cares",
    ],
    deps = [
        "backoff",
        "channel_arg_names",
        "config",
        "config_vars",
        "debug_location",
        "endpoint_addresses",
        "exec_ctx",
        "gpr",
        "grpc_grpclb_balancer_addresses",
        "grpc_resolver",
        "grpc_service_config_impl",
        "grpc_trace",
        "iomgr",
        "iomgr_timer",
        "orphanable",
        "parse_address",
        "ref_counted_ptr",
        "sockaddr_utils",
        "uri",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:grpc_check",
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
        "//src/core:sync",
        "//src/core:time",
        "//third_party/address_sorting",
    ],
)

grpc_cc_library(
    name = "httpcli",
    srcs = [
        "//src/core:util/http_client/format_request.cc",
        "//src/core:util/http_client/httpcli.cc",
        "//src/core:util/http_client/parser.cc",
    ],
    hdrs = [
        "//src/core:util/http_client/format_request.h",
        "//src/core:util/http_client/httpcli.h",
        "//src/core:util/http_client/parser.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:httpcli"],
    deps = [
        "config",
        "debug_location",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_trace",
        "handshaker",
        "iomgr",
        "orphanable",
        "ref_counted_ptr",
        "resource_quota_api",
        "transport_auth_context",
        "uri",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:event_engine_common",
        "//src/core:event_engine_shim",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:grpc_check",
        "//src/core:handshaker_registry",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:sync",
        "//src/core:tcp_connect_handshaker",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "grpc_alts_credentials",
    srcs = [
        "//src/core:credentials/transport/alts/alts_credentials.cc",
        "//src/core:credentials/transport/alts/alts_security_connector.cc",
    ],
    hdrs = [
        "//src/core:credentials/transport/alts/alts_credentials.h",
        "//src/core:credentials/transport/alts/alts_security_connector.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/status",
        "absl/strings",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "alts_util",
        "channel_arg_names",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_security_base",
        "handshaker",
        "iomgr",
        "promise",
        "ref_counted_ptr",
        "transport_auth_context",
        "tsi_alts_credentials",
        "tsi_base",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_check",
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
    external_deps = [
        "absl/log:log",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "gpr",
        "tsi_base",
        "//src/core:dump_args",
        "//src/core:grpc_check",
        "//src/core:slice",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "grpc_jwt_credentials",
    srcs = [
        "//src/core:credentials/call/jwt/json_token.cc",
        "//src/core:credentials/call/jwt/jwt_credentials.cc",
        "//src/core:credentials/call/jwt/jwt_verifier.cc",
    ],
    hdrs = [
        "//src/core:credentials/call/jwt/json_token.h",
        "//src/core:credentials/call/jwt/jwt_credentials.h",
        "//src/core:credentials/call/jwt/jwt_verifier.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "libcrypto",
        "libssl",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_trace",
        "httpcli",
        "iomgr",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "transport_auth_context",
        "uri",
        "//src/core:arena_promise",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_check",
        "//src/core:httpcli_ssl_credentials",
        "//src/core:iomgr_fwd",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:metadata_batch",
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
        "//src/core:credentials/call/json_util.cc",
        "//src/core:credentials/transport/tls/load_system_roots_fallback.cc",
        "//src/core:credentials/transport/tls/load_system_roots_supported.cc",
        "//src/core:credentials/transport/tls/load_system_roots_windows.cc",
        "//src/core:credentials/transport/tls/tls_utils.cc",
    ],
    hdrs = [
        "//src/core:credentials/call/json_util.h",
        "//src/core:credentials/transport/tls/load_system_roots.h",
        "//src/core:credentials/transport/tls/load_system_roots_supported.h",
        "//src/core:credentials/transport/tls/tls_utils.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/strings",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "config_vars",
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "transport_auth_context",
        "//src/core:error",
        "//src/core:json",
        "//src/core:load_file",
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
        "@com_google_protobuf//upb/mem",
        "absl/log",
        "absl/strings",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "alts_upb",
        "alts_util",
        "channel",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_security_base",
        "transport_auth_context",
        "tsi_alts_frame_protector",
        "tsi_base",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:pollset_set",
        "//src/core:slice",
        "//src/core:sync",
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
        "absl/log:log",
        "absl/types:span",
        "libcrypto",
        "libssl",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "gpr_platform",
        "tsi_base",
        "//src/core:grpc_check",
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
        "absl/log",
        "absl/memory",
        "libssl",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "cpp_impl_of",
        "gpr",
        "grpc_public_hdrs",
        "//src/core:grpc_check",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:sync",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_credentials",
    srcs = [
        "//src/core:credentials/transport/tls/ssl_utils.cc",
        "//src/core:tsi/ssl_transport_security.cc",
    ],
    hdrs = [
        "//src/core:credentials/transport/tls/ssl_utils.h",
        "//src/core:tsi/ssl_transport_security.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "libcrypto",
        "libssl",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "channel_arg_names",
        "config_vars",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_credentials_util",
        "grpc_public_hdrs",
        "grpc_security_base",
        "ref_counted_ptr",
        "transport_auth_context",
        "tsi_base",
        "tsi_ssl_session_cache",
        "//src/core:channel_args",
        "//src/core:env",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:grpc_crl_provider",
        "//src/core:grpc_transport_chttp2_alpn",
        "//src/core:load_file",
        "//src/core:match",
        "//src/core:ref_counted",
        "//src/core:slice",
        "//src/core:spiffe_utils",
        "//src/core:ssl_key_logging",
        "//src/core:ssl_transport_security_utils",
        "//src/core:sync",
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
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "call_tracer",
        "channel_arg_names",
        "config",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "promise",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:channelz_property_list",
        "//src/core:compression",
        "//src/core:context",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:grpc_message_size_filter",
        "//src/core:latch",
        "//src/core:latent_see",
        "//src/core:map",
        "//src/core:metadata_batch",
        "//src/core:percent_encoding",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:prioritized_race",
        "//src/core:race",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_conversion",
    ],
)

grpc_cc_library(
    name = "grpc_grpclb_balancer_addresses",
    srcs = [
        "//src/core:load_balancing/grpclb/grpclb_balancer_addresses.cc",
    ],
    hdrs = [
        "//src/core:load_balancing/grpclb/grpclb_balancer_addresses.h",
    ],
    visibility = ["//bazel:grpclb"],
    deps = [
        "endpoint_addresses",
        "gpr_platform",
        "grpc_public_hdrs",
        "//src/core:channel_args",
        "//src/core:useful",
    ],
)

grpc_cc_library(
    name = "xds_client",
    srcs = [
        "//src/core:xds/xds_client/lrs_client.cc",
        "//src/core:xds/xds_client/xds_api.cc",
        "//src/core:xds/xds_client/xds_bootstrap.cc",
        "//src/core:xds/xds_client/xds_client.cc",
    ],
    hdrs = [
        "//src/core:xds/xds_client/lrs_client.h",
        "//src/core:xds/xds_client/xds_api.h",
        "//src/core:xds/xds_client/xds_bootstrap.h",
        "//src/core:xds/xds_client/xds_channel_args.h",
        "//src/core:xds/xds_client/xds_client.h",
        "//src/core:xds/xds_client/xds_locality.h",
        "//src/core:xds/xds_client/xds_metrics.h",
        "//src/core:xds/xds_client/xds_resource_type.h",
        "//src/core:xds/xds_client/xds_resource_type_impl.h",
        "//src/core:xds/xds_client/xds_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/cleanup",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/text",
        "@com_google_protobuf//upb/json",
        "@com_google_protobuf//upb/reflection",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:xds_client_core"],
    deps = [
        "backoff",
        "debug_location",
        "endpoint_addresses",
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
        "uri",
        "work_serializer",
        "//src/core:down_cast",
        "//src/core:dual_ref_counted",
        "//src/core:env",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_check",
        "//src/core:json",
        "//src/core:per_cpu",
        "//src/core:ref_counted",
        "//src/core:ref_counted_string",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:upb_utils",
        "//src/core:useful",
        "//src/core:xds_backend_metric_propagation",
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
    deps = [
        "google_api_expr_v1alpha1_syntax_upb",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["//src/core:resolver/fake/fake_resolver.cc"],
    hdrs = ["//src/core:resolver/fake/fake_resolver.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
        "absl/time",
    ],
    visibility = [
        "//bazel:grpc_resolver_fake",
        "//test:__subpackages__",
    ],
    deps = [
        "config",
        "debug_location",
        "gpr",
        "grpc_public_hdrs",
        "grpc_resolver",
        "orphanable",
        "ref_counted_ptr",
        "uri",
        "work_serializer",
        "//src/core:channel_args",
        "//src/core:grpc_check",
        "//src/core:notification",
        "//src/core:ref_counted",
        "//src/core:sync",
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
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "gpr",
        "grpc_trace",
        "//src/core:grpc_check",
        "//src/core:http2_settings",
        "//src/core:http2_status",
        "//src/core:memory_usage",
        "//src/core:message",
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
    name = "hpack_parser_table",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser_table.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/hpack_parser_table.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/log:log",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "gpr",
        "gpr_platform",
        "grpc_trace",
        "hpack_parse_result",
        "stats",
        "//src/core:grpc_check",
        "//src/core:hpack_constants",
        "//src/core:http2_stats_collector",
        "//src/core:metadata_batch",
        "//src/core:no_destruct",
        "//src/core:parsed_metadata",
        "//src/core:slice",
        "//src/core:unique_ptr_with_bitset",
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
    ],
    deps = [
        "gpr",
        "grpc_base",
        "ref_counted_ptr",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:hpack_constants",
        "//src/core:metadata_batch",
        "//src/core:ref_counted",
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
        "absl/log:log",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "call_tracer",
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
        "//src/core:grpc_check",
        "//src/core:hpack_constants",
        "//src/core:match",
        "//src/core:metadata_batch",
        "//src/core:metadata_info",
        "//src/core:parsed_metadata",
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
    external_deps = [
        "absl/log:log",
        "absl/strings",
    ],
    deps = [
        "call_tracer",
        "chttp2_bin_encoder",
        "chttp2_legacy_frame",
        "chttp2_varint",
        "gpr",
        "gpr_platform",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_trace",
        "//src/core:grpc_check",
        "//src/core:hpack_constants",
        "//src/core:hpack_encoder_table",
        "//src/core:http2_ztrace_collector",
        "//src/core:metadata_batch",
        "//src/core:metadata_compression_traits",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:time",
        "//src/core:timeout_encoding",
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
        "//src/core:grpc_check",
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
    external_deps = [
        "absl/base:core_headers",
    ],
    deps = [
        "gpr",
        "//src/core:grpc_check",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "//src/core:ext/transport/chttp2/transport/bin_decoder.cc",
        "//src/core:ext/transport/chttp2/transport/call_tracer_wrapper.cc",
        "//src/core:ext/transport/chttp2/transport/chttp2_transport.cc",
        "//src/core:ext/transport/chttp2/transport/frame_data.cc",
        "//src/core:ext/transport/chttp2/transport/frame_goaway.cc",
        "//src/core:ext/transport/chttp2/transport/frame_ping.cc",
        "//src/core:ext/transport/chttp2/transport/frame_rst_stream.cc",
        "//src/core:ext/transport/chttp2/transport/frame_security.cc",
        "//src/core:ext/transport/chttp2/transport/frame_settings.cc",
        "//src/core:ext/transport/chttp2/transport/frame_window_update.cc",
        "//src/core:ext/transport/chttp2/transport/parsing.cc",
        "//src/core:ext/transport/chttp2/transport/stream_lists.cc",
        "//src/core:ext/transport/chttp2/transport/writing.cc",
    ],
    hdrs = [
        "//src/core:ext/transport/chttp2/transport/bin_decoder.h",
        "//src/core:ext/transport/chttp2/transport/call_tracer_wrapper.h",
        "//src/core:ext/transport/chttp2/transport/chttp2_transport.h",
        "//src/core:ext/transport/chttp2/transport/frame_data.h",
        "//src/core:ext/transport/chttp2/transport/frame_goaway.h",
        "//src/core:ext/transport/chttp2/transport/frame_ping.h",
        "//src/core:ext/transport/chttp2/transport/frame_rst_stream.h",
        "//src/core:ext/transport/chttp2/transport/frame_security.h",
        "//src/core:ext/transport/chttp2/transport/frame_settings.h",
        "//src/core:ext/transport/chttp2/transport/frame_window_update.h",
        "//src/core:ext/transport/chttp2/transport/internal.h",
        "//src/core:ext/transport/chttp2/transport/stream_lists.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/functional:bind_front",
        "absl/hash",
        "absl/log:log",
        "absl/meta:type_traits",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/random:distributions",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/time",
        "@com_google_protobuf//upb/mem",
    ],
    visibility = ["//bazel:grpclb"],
    deps = [
        "call_tracer",
        "channel_arg_names",
        "channelz",
        "chttp2_legacy_frame",
        "chttp2_varint",
        "config_vars",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_core_credentials_header",
        "grpc_public_hdrs",
        "grpc_trace",
        "hpack_encoder",
        "hpack_parser",
        "hpack_parser_table",
        "httpcli",
        "iomgr",
        "iomgr_buffer_list",
        "ref_counted_ptr",
        "stats",
        "transport_auth_context",
        "//src/core:arena",
        "//src/core:bdp_estimator",
        "//src/core:bitset",
        "//src/core:channel_args",
        "//src/core:channelz_property_list",
        "//src/core:chttp2_flow_control",
        "//src/core:closure",
        "//src/core:connectivity_state",
        "//src/core:context_list_entry",
        "//src/core:default_tcp_tracer",
        "//src/core:error",
        "//src/core:error_utils",
        "//src/core:event_engine_extensions",
        "//src/core:event_engine_query_extensions",
        "//src/core:experiments",
        "//src/core:gpr_manual_constructor",
        "//src/core:grpc_check",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//src/core:http2_stats_collector",
        "//src/core:http2_status",
        "//src/core:http2_ztrace_collector",
        "//src/core:init_internally",
        "//src/core:instrument",
        "//src/core:internal_channel_arg_names",
        "//src/core:iomgr_fwd",
        "//src/core:iomgr_port",
        "//src/core:json",
        "//src/core:match",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:metadata_info",
        "//src/core:notification",
        "//src/core:ping_abuse_policy",
        "//src/core:ping_callbacks",
        "//src/core:ping_rate_policy",
        "//src/core:poll",
        "//src/core:random_early_detection",
        "//src/core:ref_counted",
        "//src/core:resource_quota",
        "//src/core:shared_bit_gen",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:stats_data",
        "//src/core:status_conversion",
        "//src/core:status_helper",
        "//src/core:stream_quota",
        "//src/core:tcp_tracer",
        "//src/core:time",
        "//src/core:transport_common",
        "//src/core:transport_framing_endpoint_extension",
        "//src/core:useful",
        "//src/core:write_size_policy",
        "//src/proto/grpc/channelz/v2:promise_upb_proto",
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
        "@com_google_protobuf//:any_cc_proto",
    ],
)

grpc_cc_library(
    name = "subprocess",
    srcs = [
        "//src/core:util/subprocess_posix.cc",
        "//src/core:util/subprocess_windows.cc",
    ],
    hdrs = [
        "//src/core:util/subprocess.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "gpr",
        "//src/core:grpc_check",
        "//src/core:strerror",
        "//src/core:tchar",
    ],
)

grpc_cc_library(
    name = "global_callback_hook",
    srcs = [
        "src/cpp/client/global_callback_hook.cc",
    ],
    hdrs = [
        "include/grpcpp/support/global_callback_hook.h",
    ],
    external_deps = [
        "absl/base:no_destructor",
        "absl/functional:function_ref",
    ],
    deps = [
        "//src/core:grpc_check",
    ],
)

# TODO(yashykt): Remove the UPB definitions from here once they are no longer needed
### UPB Targets

grpc_upb_proto_library(
    name = "alts_handshaker_upb_proto",
    deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
)

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

grpc_upb_proto_reflection_library(
    name = "envoy_config_core_upbdefs",
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
    name = "envoy_extensions_filters_http_gcp_authn_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_gcp_authn_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg"],
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
    name = "envoy_extensions_transport_sockets_http_11_proxy_upb",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_transport_sockets_http_11_proxy_upbdefs",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_upstreams_http_upb",
    deps = ["@envoy_api//envoy/extensions/upstreams/http/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_upstreams_http_upbdefs",
    deps = ["@envoy_api//envoy/extensions/upstreams/http/v3:pkg"],
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
    deps = ["@com_github_cncf_xds//xds/type/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "xds_type_upbdefs",
    deps = ["@com_github_cncf_xds//xds/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_type_matcher_upb",
    deps = ["@com_github_cncf_xds//xds/type/matcher/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_upb",
    deps = ["@com_github_cncf_xds//xds/data/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_service_upb",
    deps = ["@com_github_cncf_xds//xds/service/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "grpc_health_upb",
    deps = ["//src/proto/grpc/health/v1:health_proto"],
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
    name = "google_api_expr_v1alpha1_syntax_upb",
    deps = ["@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto"],
)

grpc_upb_proto_library(
    name = "grpc_lb_upb",
    deps = ["//src/proto/grpc/lb/v1:load_balancer_proto"],
)

grpc_upb_proto_library(
    name = "alts_upb",
    deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
)

grpc_upb_proto_library(
    name = "rls_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_proto"],
)

grpc_upb_proto_library(
    name = "rls_config_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto"],
)

grpc_upb_proto_reflection_library(
    name = "rls_config_upbdefs",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto"],
)

grpc_upb_proto_library(
    name = "channelz_upb",
    deps = ["//src/proto/grpc/channelz/v2:channelz_proto"],
)

grpc_upb_proto_reflection_library(
    name = "channelz_upbdefs",
    deps = ["//src/proto/grpc/channelz/v2:channelz_proto"],
)

grpc_upb_proto_library(
    name = "channelz_service_upb",
    deps = ["//src/proto/grpc/channelz/v2:service_proto"],
)

grpc_upb_proto_reflection_library(
    name = "channelz_service_upbdefs",
    deps = ["//src/proto/grpc/channelz/v2:service_proto"],
)

grpc_upb_proto_library(
    name = "channelz_property_list_upb",
    deps = ["//src/proto/grpc/channelz/v2:property_list_proto"],
)

grpc_upb_proto_reflection_library(
    name = "channelz_property_list_upbdefs",
    deps = ["//src/proto/grpc/channelz/v2:property_list_proto"],
)

grpc_upb_proto_library(
    name = "promise_upb",
    deps = ["//src/proto/grpc/channelz/v2:promise_proto"],
)

grpc_upb_proto_reflection_library(
    name = "promise_upbdefs",
    deps = ["//src/proto/grpc/channelz/v2:promise_proto"],
)

grpc_upb_proto_library(
    name = "channelz_v1_upb",
    deps = ["//src/proto/grpc/channelz:channelz_proto_internal"],
)

grpc_upb_proto_reflection_library(
    name = "channelz_v1_upbdefs",
    deps = ["//src/proto/grpc/channelz:channelz_proto_internal"],
)

WELL_KNOWN_PROTO_TARGETS = [
    "any",
    "duration",
    "empty",
    "struct",
    "timestamp",
    "wrappers",
]

grpc_add_well_known_proto_upb_targets(targets = WELL_KNOWN_PROTO_TARGETS)

grpc_generate_one_off_targets()

filegroup(
    name = "root_certificates",
    srcs = [
        "etc/roots.pem",
    ],
    visibility = ["//visibility:public"],
)
