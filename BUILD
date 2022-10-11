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

config_setting(
    name = "grpc_no_ares",
    values = {"define": "grpc_no_ares=true"},
)

config_setting(
    name = "grpc_no_xds_define",
    values = {"define": "grpc_no_xds=true"},
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
    name = "ios",
    values = {"apple_platform_type": "ios"},
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
        # Disable RLS support on mobile platforms where it is not likely to be
        # needed and where reducing the binary size is more important.
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
    name = "mac_x86_64",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "use_strict_warning",
    values = {"define": "use_strict_warning=true"},
)

python_config_settings()

# This should be updated along with build_handwritten.yaml
g_stands_for = "galley"  # @unused

core_version = "28.0.0"  # @unused

version = "1.50.0"  # @unused

GPR_PUBLIC_HDRS = [
    "include/grpc/support/alloc.h",
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
]

GRPC_PUBLIC_HDRS = [
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
]

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
    "include/grpcpp/impl/call_hook.h",
    "include/grpcpp/impl/call_op_set_interface.h",
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
    "include/grpcpp/security/authorization_policy_provider.h",
    "include/grpcpp/security/tls_certificate_verifier.h",
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
    "include/grpcpp/impl/codegen/sync.h",
]

grpc_cc_library(
    name = "channel_fwd",
    hdrs = [
        "src/core/lib/channel/channel_fwd.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "transport_fwd",
    hdrs = [
        "src/core/lib/transport/transport_fwd.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "atomic_utils",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/atomic_utils.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "experiments",
    srcs = [
        "src/core/lib/experiments/config.cc",
        "src/core/lib/experiments/experiments.cc",
    ],
    hdrs = [
        "src/core/lib/experiments/config.h",
        "src/core/lib/experiments/experiments.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "gpr",
        "gpr_platform",
        "no_destruct",
    ],
)

grpc_cc_library(
    name = "init_internally",
    srcs = ["src/core/lib/surface/init_internally.cc"],
    hdrs = ["src/core/lib/surface/init_internally.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_unsecure",
    srcs = [
        "src/core/lib/surface/init.cc",
        "src/core/plugin_registry/grpc_plugin_registry.cc",
        "src/core/plugin_registry/grpc_plugin_registry_noextra.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "channel_init",
        "channel_stack_type",
        "config",
        "default_event_engine",
        "experiments",
        "forkable",
        "gpr",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_common",
        "grpc_http_filters",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
        "init_internally",
        "iomgr_timer",
        "posix_event_engine_timer_manager",
        "slice",
        "tcp_connect_handshaker",
    ],
)

GRPC_XDS_TARGETS = [
    "grpc_lb_policy_cds",
    "grpc_lb_policy_xds_cluster_impl",
    "grpc_lb_policy_xds_cluster_manager",
    "grpc_lb_policy_xds_cluster_resolver",
    "grpc_resolver_xds",
    "grpc_resolver_c2p",
    "grpc_xds_server_config_fetcher",

    # Not xDS-specific but currently only used by xDS.
    "channel_creds_registry_init",
]

grpc_cc_library(
    name = "grpc",
    srcs = [
        "src/core/lib/surface/init.cc",
        "src/core/plugin_registry/grpc_plugin_registry.cc",
        "src/core/plugin_registry/grpc_plugin_registry_extra.cc",
    ],
    defines = select({
        "grpc_no_xds": ["GRPC_NO_XDS"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    select_deps = [
        {
            "grpc_no_xds": [],
            "//conditions:default": GRPC_XDS_TARGETS,
        },
    ],
    tags = [
        "grpc_avoid_dep",
        "nofixdeps",
    ],
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "channel_init",
        "channel_stack_type",
        "config",
        "default_event_engine",
        "experiments",
        "forkable",
        "gpr",
        "grpc_alts_credentials",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_common",
        "grpc_credentials_util",
        "grpc_external_account_credentials",
        "grpc_fake_credentials",
        "grpc_google_default_credentials",
        "grpc_http_filters",
        "grpc_iam_credentials",
        "grpc_insecure_credentials",
        "grpc_jwt_credentials",
        "grpc_local_credentials",
        "grpc_oauth2_credentials",
        "grpc_security_base",
        "grpc_ssl_credentials",
        "grpc_tls_credentials",
        "grpc_trace",
        "grpc_transport_chttp2_alpn",
        "http_connect_handshaker",
        "httpcli",
        "httpcli_ssl_credentials",
        "init_internally",
        "iomgr_timer",
        "json",
        "posix_event_engine_timer_manager",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "tcp_connect_handshaker",
        "tsi_base",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "gpr_public_hdrs",
    hdrs = GPR_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
)

grpc_cc_library(
    name = "grpc_public_hdrs",
    hdrs = GRPC_PUBLIC_HDRS,
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    deps = ["gpr_public_hdrs"],
)

grpc_cc_library(
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    tags = [
        "avoid_dep",
        "nofixdeps",
    ],
    visibility = ["@grpc:public"],
    deps = ["grpc_public_hdrs"],
)

grpc_cc_library(
    name = "grpc++",
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    select_deps = [
        {
            "grpc_no_xds": [],
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
        "slice",
    ],
)

grpc_cc_library(
    name = "tchar",
    srcs = [
        "src/core/lib/gprpp/tchar.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/tchar.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc++_binder",
    srcs = [
        "src/core/ext/transport/binder/client/binder_connector.cc",
        "src/core/ext/transport/binder/client/channel_create.cc",
        "src/core/ext/transport/binder/client/channel_create_impl.cc",
        "src/core/ext/transport/binder/client/connection_id_generator.cc",
        "src/core/ext/transport/binder/client/endpoint_binder_pool.cc",
        "src/core/ext/transport/binder/client/jni_utils.cc",
        "src/core/ext/transport/binder/client/security_policy_setting.cc",
        "src/core/ext/transport/binder/security_policy/binder_security_policy.cc",
        "src/core/ext/transport/binder/server/binder_server.cc",
        "src/core/ext/transport/binder/server/binder_server_credentials.cc",
        "src/core/ext/transport/binder/transport/binder_transport.cc",
        "src/core/ext/transport/binder/utils/ndk_binder.cc",
        "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.cc",
        "src/core/ext/transport/binder/wire_format/binder_android.cc",
        "src/core/ext/transport/binder/wire_format/binder_constants.cc",
        "src/core/ext/transport/binder/wire_format/transaction.cc",
        "src/core/ext/transport/binder/wire_format/wire_reader_impl.cc",
        "src/core/ext/transport/binder/wire_format/wire_writer.cc",
    ],
    hdrs = [
        "src/core/ext/transport/binder/client/binder_connector.h",
        "src/core/ext/transport/binder/client/channel_create_impl.h",
        "src/core/ext/transport/binder/client/connection_id_generator.h",
        "src/core/ext/transport/binder/client/endpoint_binder_pool.h",
        "src/core/ext/transport/binder/client/jni_utils.h",
        "src/core/ext/transport/binder/client/security_policy_setting.h",
        "src/core/ext/transport/binder/server/binder_server.h",
        "src/core/ext/transport/binder/transport/binder_stream.h",
        "src/core/ext/transport/binder/transport/binder_transport.h",
        "src/core/ext/transport/binder/utils/binder_auto_utils.h",
        "src/core/ext/transport/binder/utils/ndk_binder.h",
        "src/core/ext/transport/binder/utils/transport_stream_receiver.h",
        "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h",
        "src/core/ext/transport/binder/wire_format/binder.h",
        "src/core/ext/transport/binder/wire_format/binder_android.h",
        "src/core/ext/transport/binder/wire_format/binder_constants.h",
        "src/core/ext/transport/binder/wire_format/transaction.h",
        "src/core/ext/transport/binder/wire_format/wire_reader.h",
        "src/core/ext/transport/binder/wire_format/wire_reader_impl.h",
        "src/core/ext/transport/binder/wire_format/wire_writer.h",
    ],
    defines = select({
        "grpc_no_binder": ["GRPC_NO_BINDER"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
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
        "arena",
        "channel_args_preconditioning",
        "channel_stack_type",
        "config",
        "debug_location",
        "gpr",
        "gpr_platform",
        "grpc",
        "grpc++_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "iomgr_fwd",
        "iomgr_port",
        "orphanable",
        "ref_counted_ptr",
        "slice",
        "slice_refcount",
        "status_helper",
        "transport_fwd",
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
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
        "grpc_codegen",
        "grpc_insecure_credentials",
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
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
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
    external_deps = [
        "absl/memory",
        "upb_lib",
    ],
    language = "c++",
    standalone = True,
    visibility = ["@grpc:tsi"],
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
        "src/core/ext/filters/census/grpc_context.cc",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc/census.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "useful",
    hdrs = ["src/core/lib/gpr/useful.h"],
    external_deps = [
        "absl/strings",
        "absl/types:variant",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "examine_stack",
    srcs = [
        "src/core/lib/gprpp/examine_stack.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/examine_stack.h",
    ],
    external_deps = ["absl/types:optional"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "gpr_atm",
    srcs = [
        "src/core/lib/gpr/atm.cc",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpc/support/atm.h",
    ],
    deps = [
        "gpr_platform",
        "gpr_public_hdrs",
        "useful",
    ],
)

grpc_cc_library(
    name = "gpr_manual_constructor",
    srcs = [],
    hdrs = [
        "src/core/lib/gprpp/manual_constructor.h",
    ],
    language = "c++",
    deps = [
        "construct_destruct",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "gpr_murmur_hash",
    srcs = [
        "src/core/lib/gpr/murmur_hash.cc",
    ],
    hdrs = [
        "src/core/lib/gpr/murmur_hash.h",
    ],
    external_deps = ["absl/base:core_headers"],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "gpr_spinlock",
    srcs = [],
    hdrs = [
        "src/core/lib/gpr/spinlock.h",
    ],
    language = "c++",
    deps = [
        "gpr_atm",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "gpr_log_internal",
    hdrs = [
        "src/core/lib/gpr/log_internal.h",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "env",
    srcs = [
        "src/core/lib/gprpp/env_linux.cc",
        "src/core/lib/gprpp/env_posix.cc",
        "src/core/lib/gprpp/env_windows.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/env.h",
    ],
    external_deps = ["absl/types:optional"],
    deps = [
        "gpr_platform",
        "tchar",
    ],
)

grpc_cc_library(
    name = "gpr",
    srcs = [
        "src/core/lib/gpr/alloc.cc",
        "src/core/lib/gpr/cpu_iphone.cc",
        "src/core/lib/gpr/cpu_linux.cc",
        "src/core/lib/gpr/cpu_posix.cc",
        "src/core/lib/gpr/cpu_windows.cc",
        "src/core/lib/gpr/log.cc",
        "src/core/lib/gpr/log_android.cc",
        "src/core/lib/gpr/log_linux.cc",
        "src/core/lib/gpr/log_posix.cc",
        "src/core/lib/gpr/log_windows.cc",
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
        "src/core/lib/gpr/tmpfile_msys.cc",
        "src/core/lib/gpr/tmpfile_posix.cc",
        "src/core/lib/gpr/tmpfile_windows.cc",
        "src/core/lib/gpr/wrap_memcpy.cc",
        "src/core/lib/gprpp/fork.cc",
        "src/core/lib/gprpp/global_config_env.cc",
        "src/core/lib/gprpp/host_port.cc",
        "src/core/lib/gprpp/mpscq.cc",
        "src/core/lib/gprpp/stat_posix.cc",
        "src/core/lib/gprpp/stat_windows.cc",
        "src/core/lib/gprpp/thd_posix.cc",
        "src/core/lib/gprpp/thd_windows.cc",
        "src/core/lib/gprpp/time_util.cc",
    ],
    hdrs = [
        "src/core/lib/gpr/alloc.h",
        "src/core/lib/gpr/string.h",
        "src/core/lib/gpr/time_precise.h",
        "src/core/lib/gpr/tmpfile.h",
        "src/core/lib/gprpp/fork.h",
        "src/core/lib/gprpp/global_config.h",
        "src/core/lib/gprpp/global_config_custom.h",
        "src/core/lib/gprpp/global_config_env.h",
        "src/core/lib/gprpp/global_config_generic.h",
        "src/core/lib/gprpp/host_port.h",
        "src/core/lib/gprpp/memory.h",
        "src/core/lib/gprpp/mpscq.h",
        "src/core/lib/gprpp/stat.h",
        "src/core/lib/gprpp/sync.h",
        "src/core/lib/gprpp/thd.h",
        "src/core/lib/gprpp/time_util.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
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
        "construct_destruct",
        "env",
        "examine_stack",
        "gpr_atm",
        "gpr_tls",
        "no_destruct",
        "tchar",
        "useful",
    ],
)

grpc_cc_library(
    name = "gpr_tls",
    hdrs = ["src/core/lib/gpr/tls.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "chunked_vector",
    hdrs = ["src/core/lib/gprpp/chunked_vector.h"],
    deps = [
        "arena",
        "gpr",
        "gpr_manual_constructor",
    ],
)

grpc_cc_library(
    name = "construct_destruct",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/construct_destruct.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "cpp_impl_of",
    hdrs = ["src/core/lib/gprpp/cpp_impl_of.h"],
    language = "c++",
)

grpc_cc_library(
    name = "status_helper",
    srcs = [
        "src/core/lib/gprpp/status_helper.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/status_helper.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/time",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "debug_location",
        "google_rpc_status_upb",
        "gpr",
        "percent_encoding",
        "protobuf_any_upb",
        "slice",
    ],
)

grpc_cc_library(
    name = "unique_type_name",
    hdrs = ["src/core/lib/gprpp/unique_type_name.h"],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "gpr_platform",
        "useful",
    ],
)

grpc_cc_library(
    name = "work_serializer",
    srcs = [
        "src/core/lib/gprpp/work_serializer.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/work_serializer.h",
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
    name = "validation_errors",
    srcs = [
        "src/core/lib/gprpp/validation_errors.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/validation_errors.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    language = "c++",
    deps = ["gpr_platform"],
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
    name = "grpc_trace",
    srcs = ["src/core/lib/debug/trace.cc"],
    hdrs = ["src/core/lib/debug/trace.h"],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["@grpc:trace"],
    deps = [
        "gpr",
        "grpc_codegen",
        "grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "config",
    srcs = [
        "src/core/lib/config/core_configuration.cc",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/config/core_configuration.h",
    ],
    visibility = ["@grpc:client_channel"],
    deps = [
        "certificate_provider_registry",
        "channel_args_preconditioning",
        "channel_creds_registry",
        "channel_init",
        "gpr",
        "grpc_resolver",
        "handshaker_registry",
        "lb_policy_registry",
        "proxy_mapper_registry",
        "service_config_parser",
    ],
)

grpc_cc_library(
    name = "debug_location",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/debug_location.h"],
    visibility = ["@grpc:debug_location"],
)

grpc_cc_library(
    name = "overload",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/overload.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "match",
    external_deps = ["absl/types:variant"],
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/match.h"],
    deps = [
        "gpr_platform",
        "overload",
    ],
)

grpc_cc_library(
    name = "table",
    external_deps = [
        "absl/meta:type_traits",
        "absl/utility",
    ],
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/table.h"],
    deps = [
        "bitset",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "packed_table",
    hdrs = ["src/core/lib/gprpp/packed_table.h"],
    language = "c++",
    deps = [
        "gpr_public_hdrs",
        "sorted_pack",
        "table",
    ],
)

grpc_cc_library(
    name = "bitset",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/bitset.h"],
    deps = [
        "gpr_platform",
        "useful",
    ],
)

grpc_cc_library(
    name = "no_destruct",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/no_destruct.h"],
    deps = [
        "construct_destruct",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "orphanable",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/orphanable.h"],
    visibility = ["@grpc:client_channel"],
    deps = [
        "debug_location",
        "gpr_platform",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "poll",
    external_deps = ["absl/types:variant"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/poll.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "call_push_pull",
    hdrs = ["src/core/lib/promise/call_push_pull.h"],
    external_deps = ["absl/types:variant"],
    language = "c++",
    deps = [
        "bitset",
        "construct_destruct",
        "gpr_platform",
        "poll",
        "promise_like",
        "promise_status",
    ],
)

grpc_cc_library(
    name = "context",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/context.h",
    ],
    deps = [
        "gpr_platform",
        "gpr_tls",
    ],
)

grpc_cc_library(
    name = "map",
    external_deps = ["absl/types:variant"],
    language = "c++",
    public_hdrs = ["src/core/lib/promise/map.h"],
    deps = [
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "sleep",
    srcs = [
        "src/core/lib/promise/sleep.cc",
    ],
    hdrs = [
        "src/core/lib/promise/sleep.h",
    ],
    external_deps = ["absl/status"],
    deps = [
        "activity",
        "default_event_engine",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "poll",
        "time",
    ],
)

grpc_cc_library(
    name = "promise",
    external_deps = [
        "absl/status",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/promise.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "arena_promise",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/arena_promise.h",
    ],
    deps = [
        "arena",
        "context",
        "gpr_platform",
        "poll",
    ],
)

grpc_cc_library(
    name = "promise_like",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/promise_like.h",
    ],
    deps = [
        "gpr_platform",
        "poll",
    ],
)

grpc_cc_library(
    name = "promise_factory",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/promise_factory.h",
    ],
    deps = [
        "gpr_platform",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "if",
    external_deps = [
        "absl/status:statusor",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = ["src/core/lib/promise/if.h"],
    deps = [
        "gpr_platform",
        "poll",
        "promise_factory",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "promise_status",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/status.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "race",
    external_deps = ["absl/types:variant"],
    language = "c++",
    public_hdrs = ["src/core/lib/promise/race.h"],
    deps = [
        "gpr_platform",
        "poll",
    ],
)

grpc_cc_library(
    name = "loop",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/loop.h",
    ],
    deps = [
        "gpr_platform",
        "poll",
        "promise_factory",
    ],
)

grpc_cc_library(
    name = "basic_join",
    external_deps = [
        "absl/types:variant",
        "absl/utility",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/basic_join.h",
    ],
    deps = [
        "bitset",
        "construct_destruct",
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "join",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/join.h",
    ],
    deps = [
        "basic_join",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_join",
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/try_join.h",
    ],
    deps = [
        "basic_join",
        "gpr_platform",
        "poll",
        "promise_status",
    ],
)

grpc_cc_library(
    name = "basic_seq",
    external_deps = [
        "absl/meta:type_traits",
        "absl/types:variant",
        "absl/utility",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/basic_seq.h",
    ],
    deps = [
        "construct_destruct",
        "gpr_platform",
        "poll",
        "promise_factory",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "seq",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/seq.h",
    ],
    deps = [
        "basic_seq",
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "try_seq",
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/try_seq.h",
    ],
    deps = [
        "basic_seq",
        "gpr_platform",
        "poll",
        "promise_like",
        "promise_status",
    ],
)

grpc_cc_library(
    name = "activity",
    srcs = [
        "src/core/lib/promise/activity.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/activity.h",
    ],
    deps = [
        "atomic_utils",
        "construct_destruct",
        "context",
        "gpr",
        "gpr_tls",
        "no_destruct",
        "orphanable",
        "poll",
        "promise_factory",
        "promise_status",
    ],
)

grpc_cc_library(
    name = "exec_ctx_wakeup_scheduler",
    hdrs = [
        "src/core/lib/promise/exec_ctx_wakeup_scheduler.h",
    ],
    language = "c++",
    deps = [
        "closure",
        "debug_location",
        "error",
        "exec_ctx",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "wait_set",
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/wait_set.h",
    ],
    deps = [
        "activity",
        "gpr_platform",
        "poll",
    ],
)

grpc_cc_library(
    name = "intra_activity_waiter",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/intra_activity_waiter.h",
    ],
    deps = [
        "activity",
        "gpr_platform",
        "poll",
    ],
)

grpc_cc_library(
    name = "latch",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/latch.h",
    ],
    deps = [
        "gpr",
        "intra_activity_waiter",
        "poll",
    ],
)

grpc_cc_library(
    name = "observable",
    external_deps = [
        "absl/base:core_headers",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/observable.h",
    ],
    deps = [
        "activity",
        "gpr",
        "poll",
        "promise_like",
        "wait_set",
    ],
)

grpc_cc_library(
    name = "pipe",
    external_deps = ["absl/types:optional"],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/pipe.h",
    ],
    deps = [
        "arena",
        "context",
        "gpr",
        "intra_activity_waiter",
        "poll",
    ],
)

grpc_cc_library(
    name = "for_each",
    external_deps = [
        "absl/status",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = ["src/core/lib/promise/for_each.h"],
    deps = [
        "gpr_platform",
        "poll",
        "promise_factory",
    ],
)

grpc_cc_library(
    name = "ref_counted",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/ref_counted.h"],
    deps = [
        "atomic_utils",
        "debug_location",
        "gpr",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "dual_ref_counted",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/dual_ref_counted.h"],
    deps = [
        "debug_location",
        "gpr",
        "orphanable",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "ref_counted_ptr",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/ref_counted_ptr.h"],
    visibility = ["@grpc:ref_counted_ptr"],
    deps = [
        "debug_location",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "handshaker",
    srcs = [
        "src/core/lib/transport/handshaker.cc",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/strings:str_format",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/handshaker.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "closure",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_trace",
        "iomgr_timer",
        "ref_counted",
        "ref_counted_ptr",
        "slice",
        "slice_buffer",
        "slice_refcount",
        "time",
    ],
)

grpc_cc_library(
    name = "handshaker_factory",
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/handshaker_factory.h",
    ],
    deps = [
        "channel_args",
        "gpr_platform",
        "iomgr_fwd",
    ],
)

grpc_cc_library(
    name = "handshaker_registry",
    srcs = [
        "src/core/lib/transport/handshaker_registry.cc",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/handshaker_registry.h",
    ],
    deps = [
        "channel_args",
        "gpr_platform",
        "handshaker_factory",
        "iomgr_fwd",
    ],
)

grpc_cc_library(
    name = "http_connect_handshaker",
    srcs = [
        "src/core/lib/transport/http_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/http_connect_handshaker.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "closure",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "handshaker",
        "handshaker_factory",
        "handshaker_registry",
        "httpcli",
        "iomgr_fwd",
        "ref_counted_ptr",
        "slice",
        "slice_buffer",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "tcp_connect_handshaker",
    srcs = [
        "src/core/lib/transport/tcp_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/tcp_connect_handshaker.h",
    ],
    deps = [
        "channel_args",
        "closure",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "handshaker",
        "handshaker_factory",
        "handshaker_registry",
        "iomgr_fwd",
        "pollset_set",
        "ref_counted_ptr",
        "resolved_address",
        "slice",
        "slice_refcount",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry",
    hdrs = [
        "src/core/lib/security/credentials/channel_creds_registry.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "gpr_platform",
        "json",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "event_engine_memory_allocator",
    srcs = [
        "src/core/lib/event_engine/memory_allocator.cc",
    ],
    hdrs = [
        "include/grpc/event_engine/internal/memory_allocator_impl.h",
        "include/grpc/event_engine/memory_allocator.h",
        "include/grpc/event_engine/memory_request.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "gpr_platform",
        "slice",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "memory_quota",
    srcs = [
        "src/core/lib/resource_quota/memory_quota.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/memory_quota.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "activity",
        "event_engine_memory_allocator",
        "exec_ctx_wakeup_scheduler",
        "experiments",
        "gpr",
        "grpc_trace",
        "loop",
        "map",
        "orphanable",
        "periodic_update",
        "poll",
        "race",
        "ref_counted_ptr",
        "resource_quota_trace",
        "seq",
        "time",
        "useful",
    ],
)

grpc_cc_library(
    name = "periodic_update",
    srcs = [
        "src/core/lib/resource_quota/periodic_update.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/periodic_update.h",
    ],
    external_deps = ["absl/functional:function_ref"],
    deps = [
        "gpr_platform",
        "time",
        "useful",
    ],
)

grpc_cc_library(
    name = "arena",
    srcs = [
        "src/core/lib/resource_quota/arena.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/arena.h",
    ],
    deps = [
        "construct_destruct",
        "context",
        "event_engine_memory_allocator",
        "gpr",
        "memory_quota",
    ],
)

grpc_cc_library(
    name = "thread_quota",
    srcs = [
        "src/core/lib/resource_quota/thread_quota.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/thread_quota.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "gpr",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "resource_quota_trace",
    srcs = [
        "src/core/lib/resource_quota/trace.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/trace.h",
    ],
    deps = [
        "gpr_platform",
        "grpc_trace",
    ],
)

grpc_cc_library(
    name = "resource_quota",
    srcs = [
        "src/core/lib/resource_quota/resource_quota.cc",
    ],
    hdrs = [
        "src/core/lib/resource_quota/resource_quota.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "cpp_impl_of",
        "gpr_platform",
        "grpc_codegen",
        "memory_quota",
        "ref_counted",
        "ref_counted_ptr",
        "thread_quota",
        "useful",
    ],
)

grpc_cc_library(
    name = "slice_refcount",
    srcs = [
        "src/core/lib/slice/slice_refcount.cc",
    ],
    hdrs = [
        "src/core/lib/slice/slice_refcount.h",
        "src/core/lib/slice/slice_refcount_base.h",
    ],
    public_hdrs = [
        "include/grpc/slice.h",
    ],
    deps = [
        "gpr",
        "grpc_codegen",
    ],
)

grpc_cc_library(
    name = "slice",
    srcs = [
        "src/core/lib/slice/slice.cc",
        "src/core/lib/slice/slice_string_helpers.cc",
    ],
    hdrs = [
        "include/grpc/slice.h",
        "src/core/lib/slice/slice.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "gpr",
        "gpr_murmur_hash",
        "grpc_codegen",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "slice_buffer",
    srcs = [
        "src/core/lib/slice/slice_buffer.cc",
    ],
    hdrs = [
        "include/grpc/slice_buffer.h",
        "src/core/lib/slice/slice_buffer.h",
    ],
    deps = [
        "gpr",
        "slice",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "error",
    srcs = [
        "src/core/lib/iomgr/error.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/error.h",
    ],
    external_deps = [
        "absl/status",
    ],
    tags = ["nofixdeps"],
    deps = [
        "gpr",
        "gpr_spinlock",
        "grpc_codegen",
        "grpc_trace",
        "slice",
        "slice_refcount",
        "status_helper",
        "useful",
    ],
)

grpc_cc_library(
    name = "closure",
    hdrs = [
        "src/core/lib/iomgr/closure.h",
    ],
    deps = [
        "debug_location",
        "error",
        "gpr",
        "gpr_manual_constructor",
    ],
)

grpc_cc_library(
    name = "time",
    srcs = [
        "src/core/lib/gprpp/time.cc",
    ],
    hdrs = [
        "src/core/lib/gprpp/time.h",
    ],
    external_deps = [
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr",
        "gpr_tls",
        "no_destruct",
        "useful",
    ],
)

grpc_cc_library(
    name = "exec_ctx",
    srcs = [
        "src/core/lib/iomgr/combiner.cc",
        "src/core/lib/iomgr/exec_ctx.cc",
        "src/core/lib/iomgr/executor.cc",
        "src/core/lib/iomgr/iomgr_internal.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/iomgr_internal.h",
    ],
    deps = [
        "closure",
        "debug_location",
        "error",
        "gpr",
        "gpr_atm",
        "gpr_spinlock",
        "gpr_tls",
        "grpc_codegen",
        "grpc_trace",
        "time",
        "useful",
    ],
)

grpc_cc_library(
    name = "sockaddr_utils",
    srcs = [
        "src/core/lib/address_utils/sockaddr_utils.cc",
    ],
    hdrs = [
        "src/core/lib/address_utils/sockaddr_utils.h",
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
        "grpc_sockaddr",
        "iomgr_port",
        "resolved_address",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "iomgr_port",
    hdrs = [
        "src/core/lib/iomgr/port.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "iomgr_timer",
    srcs = [
        "src/core/lib/iomgr/timer.cc",
        "src/core/lib/iomgr/timer_generic.cc",
        "src/core/lib/iomgr/timer_heap.cc",
        "src/core/lib/iomgr/timer_manager.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/timer.h",
        "src/core/lib/iomgr/timer_generic.h",
        "src/core/lib/iomgr/timer_heap.h",
        "src/core/lib/iomgr/timer_manager.h",
    ] + [
        # TODO(hork): deduplicate
        "src/core/lib/iomgr/iomgr.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    visibility = ["@grpc:iomgr_timer"],
    deps = [
        "closure",
        "event_engine_base_hdrs",
        "exec_ctx",
        "gpr",
        "gpr_manual_constructor",
        "gpr_platform",
        "gpr_spinlock",
        "gpr_tls",
        "grpc_trace",
        "iomgr_port",
        "time",
        "time_averaged_stats",
        "useful",
    ],
)

grpc_cc_library(
    name = "iomgr_fwd",
    hdrs = [
        "src/core/lib/iomgr/iomgr_fwd.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_sockaddr",
    srcs = [
        "src/core/lib/iomgr/sockaddr_utils_posix.cc",
        "src/core/lib/iomgr/socket_utils_windows.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_posix.h",
        "src/core/lib/iomgr/sockaddr_windows.h",
        "src/core/lib/iomgr/socket_utils.h",
    ],
    deps = [
        "gpr",
        "iomgr_port",
    ],
)

grpc_cc_library(
    name = "avl",
    hdrs = [
        "src/core/lib/avl/avl.h",
    ],
    deps = [
        "gpr_platform",
        "useful",
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
    tags = ["nofixdeps"],
    deps = [
        "gpr",
    ],
)

grpc_cc_library(
    name = "time_averaged_stats",
    srcs = ["src/core/lib/gprpp/time_averaged_stats.cc"],
    hdrs = [
        "src/core/lib/gprpp/time_averaged_stats.h",
    ],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "forkable",
    srcs = [
        "src/core/lib/event_engine/forkable.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/forkable.h",
    ],
    external_deps = ["absl/container:flat_hash_set"],
    deps = [
        "gpr",
        "gpr_platform",
        "no_destruct",
    ],
)

grpc_cc_library(
    name = "event_engine_poller",
    hdrs = [
        "src/core/lib/event_engine/poller.h",
    ],
    external_deps = ["absl/functional:function_ref"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_executor",
    hdrs = [
        "src/core/lib/event_engine/executor/executor.h",
    ],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_time_util",
    srcs = ["src/core/lib/event_engine/time_util.cc"],
    hdrs = ["src/core/lib/event_engine/time_util.h"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_threaded_executor",
    srcs = [
        "src/core/lib/event_engine/executor/threaded_executor.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/executor/threaded_executor.h",
    ],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "event_engine_base_hdrs",
        "event_engine_executor",
        "event_engine_thread_pool",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "common_event_engine_closures",
    hdrs = ["src/core/lib/event_engine/common_closures.h"],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_timer",
    srcs = [
        "src/core/lib/event_engine/posix_engine/timer.cc",
        "src/core/lib/event_engine/posix_engine/timer_heap.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/timer.h",
        "src/core/lib/event_engine/posix_engine/timer_heap.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr",
        "time",
        "time_averaged_stats",
        "useful",
    ],
)

grpc_cc_library(
    name = "event_engine_thread_pool",
    srcs = ["src/core/lib/event_engine/thread_pool.cc"],
    hdrs = [
        "src/core/lib/event_engine/thread_pool.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/time",
    ],
    deps = [
        "forkable",
        "gpr",
        "gpr_tls",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_timer_manager",
    srcs = ["src/core/lib/event_engine/posix_engine/timer_manager.cc"],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/timer_manager.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/time",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_base_hdrs",
        "forkable",
        "gpr",
        "gpr_tls",
        "grpc_trace",
        "posix_event_engine_timer",
        "time",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_event_poller",
    srcs = [],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/event_poller.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "event_engine_base_hdrs",
        "event_engine_poller",
        "gpr_platform",
        "posix_event_engine_closure",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_closure",
    srcs = [],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/posix_engine_closure.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_lockfree_event",
    srcs = [
        "src/core/lib/event_engine/posix_engine/lockfree_event.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/lockfree_event.h",
    ],
    external_deps = ["absl/status"],
    deps = [
        "gpr",
        "gpr_atm",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "status_helper",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix",
    hdrs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h",
    ],
    external_deps = ["absl/status"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_pipe",
    srcs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "gpr",
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_eventfd",
    srcs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "gpr",
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_default",
    srcs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "gpr_platform",
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_eventfd",
        "posix_event_engine_wakeup_fd_posix_pipe",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_epoll1",
    srcs = [
        "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/ev_epoll1_linux.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
    ],
    deps = [
        "event_engine_base_hdrs",
        "event_engine_poller",
        "event_engine_time_util",
        "gpr",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_lockfree_event",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_default",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_poll",
    srcs = [
        "src/core/lib/event_engine/posix_engine/ev_poll_posix.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/ev_poll_posix.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
    ],
    deps = [
        "common_event_engine_closures",
        "event_engine_base_hdrs",
        "event_engine_poller",
        "event_engine_time_util",
        "gpr",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_default",
        "time",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_default",
    srcs = [
        "src/core/lib/event_engine/posix_engine/event_poller_posix_default.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/event_poller_posix_default.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "gpr",
        "posix_event_engine_event_poller",
        "posix_event_engine_poller_posix_epoll1",
        "posix_event_engine_poller_posix_poll",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_internal_errqueue",
    srcs = [
        "src/core/lib/event_engine/posix_engine/internal_errqueue.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/internal_errqueue.h",
    ],
    deps = [
        "gpr",
        "iomgr_port",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_traced_buffer_list",
    srcs = [
        "src/core/lib/event_engine/posix_engine/traced_buffer_list.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/traced_buffer_list.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/types:optional",
    ],
    deps = [
        "gpr",
        "iomgr_port",
        "posix_event_engine_internal_errqueue",
    ],
)

grpc_cc_library(
    name = "event_engine_utils",
    srcs = ["src/core/lib/event_engine/utils.cc"],
    hdrs = ["src/core/lib/event_engine/utils.h"],
    external_deps = ["absl/strings"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
        "time",
    ],
)

grpc_cc_library(
    name = "event_engine_socket_notifier",
    hdrs = ["src/core/lib/event_engine/socket_notifier.h"],
    external_deps = ["absl/status"],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_tcp_socket_utils",
    srcs = [
        "src/core/lib/event_engine/posix_engine/tcp_socket_utils.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/utility",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr",
        "grpc_codegen",
        "iomgr_port",
        "ref_counted_ptr",
        "resource_quota",
        "socket_mutator",
        "status_helper",
        "useful",
    ],
)

grpc_cc_library(
    name = "posix_event_engine",
    srcs = ["src/core/lib/event_engine/posix_engine/posix_engine.cc"],
    hdrs = ["src/core/lib/event_engine/posix_engine/posix_engine.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "event_engine_base_hdrs",
        "event_engine_common",
        "event_engine_threaded_executor",
        "event_engine_trace",
        "event_engine_utils",
        "gpr",
        "grpc_trace",
        "posix_event_engine_timer",
        "posix_event_engine_timer_manager",
    ],
)

grpc_cc_library(
    name = "windows_event_engine",
    srcs = ["src/core/lib/event_engine/windows/windows_engine.cc"],
    hdrs = ["src/core/lib/event_engine/windows/windows_engine.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "event_engine_base_hdrs",
        "event_engine_common",
        "event_engine_threaded_executor",
        "event_engine_trace",
        "event_engine_utils",
        "gpr",
        "posix_event_engine_timer_manager",
        "time",
        "windows_iocp",
    ],
)

grpc_cc_library(
    name = "windows_iocp",
    srcs = [
        "src/core/lib/event_engine/windows/iocp.cc",
        "src/core/lib/event_engine/windows/win_socket.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/windows/iocp.h",
        "src/core/lib/event_engine/windows/win_socket.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "event_engine_base_hdrs",
        "event_engine_executor",
        "event_engine_poller",
        "event_engine_socket_notifier",
        "event_engine_time_util",
        "event_engine_trace",
        "gpr",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_common",
    srcs = [
        "src/core/lib/event_engine/resolved_address.cc",
        "src/core/lib/event_engine/slice.cc",
        "src/core/lib/event_engine/slice_buffer.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/handle_containers.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr",
        "gpr_platform",
        "slice",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "event_engine_trace",
    srcs = [
        "src/core/lib/event_engine/trace.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/trace.h",
    ],
    deps = [
        "gpr",
        "gpr_platform",
        "grpc_trace",
    ],
)

# NOTE: this target gets replaced inside Google's build system to be one that
# integrates with other internal systems better. Please do not rename or fold
# this into other targets.
grpc_cc_library(
    name = "default_event_engine_factory",
    srcs = ["src/core/lib/event_engine/default_event_engine_factory.cc"],
    hdrs = ["src/core/lib/event_engine/default_event_engine_factory.h"],
    external_deps = ["absl/memory"],
    select_deps = [{
        "//:windows": ["windows_event_engine"],
        "//:windows_msvc": ["windows_event_engine"],
        "//:windows_other": ["windows_event_engine"],
        "//conditions:default": ["posix_event_engine"],
    }],
    deps = [
        "event_engine_base_hdrs",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "default_event_engine",
    srcs = [
        "src/core/lib/event_engine/default_event_engine.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/default_event_engine.h",
    ],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "default_event_engine_factory",
        "event_engine_base_hdrs",
        "gpr",
    ],
)

grpc_cc_library(
    name = "uri_parser",
    srcs = [
        "src/core/lib/uri/uri_parser.cc",
    ],
    hdrs = [
        "src/core/lib/uri/uri_parser.h",
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
    name = "channel_args_preconditioning",
    srcs = [
        "src/core/lib/channel/channel_args_preconditioning.cc",
    ],
    hdrs = [
        "src/core/lib/channel/channel_args_preconditioning.h",
    ],
    deps = [
        "channel_args",
        "gpr_platform",
        "grpc_codegen",
    ],
)

grpc_cc_library(
    name = "pid_controller",
    srcs = [
        "src/core/lib/transport/pid_controller.cc",
    ],
    hdrs = [
        "src/core/lib/transport/pid_controller.h",
    ],
    deps = [
        "gpr_platform",
        "useful",
    ],
)

grpc_cc_library(
    name = "bdp_estimator",
    srcs = [
        "src/core/lib/transport/bdp_estimator.cc",
    ],
    hdrs = ["src/core/lib/transport/bdp_estimator.h"],
    deps = [
        "gpr",
        "grpc_trace",
        "time",
    ],
)

grpc_cc_library(
    name = "percent_encoding",
    srcs = [
        "src/core/lib/slice/percent_encoding.cc",
    ],
    hdrs = [
        "src/core/lib/slice/percent_encoding.h",
    ],
    deps = [
        "bitset",
        "gpr",
        "slice",
    ],
)

grpc_cc_library(
    name = "socket_mutator",
    srcs = [
        "src/core/lib/iomgr/socket_mutator.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/socket_mutator.h",
    ],
    deps = [
        "channel_args",
        "gpr",
        "grpc_codegen",
        "useful",
    ],
)

grpc_cc_library(
    name = "backoff",
    srcs = [
        "src/core/lib/backoff/backoff.cc",
    ],
    hdrs = [
        "src/core/lib/backoff/backoff.h",
    ],
    external_deps = ["absl/random"],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_platform",
        "time",
    ],
)

grpc_cc_library(
    name = "pollset_set",
    srcs = [
        "src/core/lib/iomgr/pollset_set.cc",
    ],
    hdrs = [
        "src/core/lib/iomgr/pollset_set.h",
    ],
    deps = [
        "gpr",
        "iomgr_fwd",
    ],
)

grpc_cc_library(
    name = "grpc_base",
    srcs = [
        "src/core/lib/address_utils/parse_address.cc",
        "src/core/lib/channel/channel_stack.cc",
        "src/core/lib/channel/channel_stack_builder_impl.cc",
        "src/core/lib/channel/channel_trace.cc",
        "src/core/lib/channel/channelz.cc",
        "src/core/lib/channel/channelz_registry.cc",
        "src/core/lib/channel/connected_channel.cc",
        "src/core/lib/channel/promise_based_filter.cc",
        "src/core/lib/channel/status_util.cc",
        "src/core/lib/compression/compression.cc",
        "src/core/lib/compression/compression_internal.cc",
        "src/core/lib/compression/message_compress.cc",
        "src/core/lib/debug/stats.cc",
        "src/core/lib/debug/stats_data.cc",
        "src/core/lib/event_engine/channel_args_endpoint_config.cc",
        "src/core/lib/iomgr/buffer_list.cc",
        "src/core/lib/iomgr/call_combiner.cc",
        "src/core/lib/iomgr/cfstream_handle.cc",
        "src/core/lib/iomgr/dualstack_socket_posix.cc",
        "src/core/lib/iomgr/endpoint.cc",
        "src/core/lib/iomgr/endpoint_cfstream.cc",
        "src/core/lib/iomgr/endpoint_pair_posix.cc",
        "src/core/lib/iomgr/endpoint_pair_windows.cc",
        "src/core/lib/iomgr/error_cfstream.cc",
        "src/core/lib/iomgr/ev_apple.cc",
        "src/core/lib/iomgr/ev_epoll1_linux.cc",
        "src/core/lib/iomgr/ev_poll_posix.cc",
        "src/core/lib/iomgr/ev_posix.cc",
        "src/core/lib/iomgr/ev_windows.cc",
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
        "src/core/lib/iomgr/iomgr_posix.cc",
        "src/core/lib/iomgr/iomgr_posix_cfstream.cc",
        "src/core/lib/iomgr/iomgr_windows.cc",
        "src/core/lib/iomgr/load_file.cc",
        "src/core/lib/iomgr/lockfree_event.cc",
        "src/core/lib/iomgr/polling_entity.cc",
        "src/core/lib/iomgr/pollset.cc",
        "src/core/lib/iomgr/pollset_set_windows.cc",
        "src/core/lib/iomgr/pollset_windows.cc",
        "src/core/lib/iomgr/resolve_address.cc",
        "src/core/lib/iomgr/resolve_address_posix.cc",
        "src/core/lib/iomgr/resolve_address_windows.cc",
        "src/core/lib/iomgr/socket_factory_posix.cc",
        "src/core/lib/iomgr/socket_mutator.cc",
        "src/core/lib/iomgr/socket_utils_common_posix.cc",
        "src/core/lib/iomgr/socket_utils_linux.cc",
        "src/core/lib/iomgr/socket_utils_posix.cc",
        "src/core/lib/iomgr/socket_windows.cc",
        "src/core/lib/iomgr/tcp_client.cc",
        "src/core/lib/iomgr/tcp_client_cfstream.cc",
        "src/core/lib/iomgr/tcp_client_posix.cc",
        "src/core/lib/iomgr/tcp_client_windows.cc",
        "src/core/lib/iomgr/tcp_posix.cc",
        "src/core/lib/iomgr/tcp_server.cc",
        "src/core/lib/iomgr/tcp_server_posix.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_common.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc",
        "src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc",
        "src/core/lib/iomgr/tcp_server_windows.cc",
        "src/core/lib/iomgr/tcp_windows.cc",
        "src/core/lib/iomgr/unix_sockets_posix.cc",
        "src/core/lib/iomgr/unix_sockets_posix_noop.cc",
        "src/core/lib/iomgr/wakeup_fd_eventfd.cc",
        "src/core/lib/iomgr/wakeup_fd_nospecial.cc",
        "src/core/lib/iomgr/wakeup_fd_pipe.cc",
        "src/core/lib/iomgr/wakeup_fd_posix.cc",
        "src/core/lib/resource_quota/api.cc",
        "src/core/lib/slice/b64.cc",
        "src/core/lib/slice/slice_api.cc",
        "src/core/lib/slice/slice_buffer_api.cc",
        "src/core/lib/surface/api_trace.cc",
        "src/core/lib/surface/builtins.cc",
        "src/core/lib/surface/byte_buffer.cc",
        "src/core/lib/surface/byte_buffer_reader.cc",
        "src/core/lib/surface/call.cc",
        "src/core/lib/surface/call_details.cc",
        "src/core/lib/surface/call_log_batch.cc",
        "src/core/lib/surface/channel.cc",
        "src/core/lib/surface/channel_ping.cc",
        "src/core/lib/surface/completion_queue.cc",
        "src/core/lib/surface/completion_queue_factory.cc",
        "src/core/lib/surface/event_string.cc",
        "src/core/lib/surface/lame_client.cc",
        "src/core/lib/surface/metadata_array.cc",
        "src/core/lib/surface/server.cc",
        "src/core/lib/surface/validate_metadata.cc",
        "src/core/lib/surface/version.cc",
        "src/core/lib/transport/connectivity_state.cc",
        "src/core/lib/transport/error_utils.cc",
        "src/core/lib/transport/metadata_batch.cc",
        "src/core/lib/transport/parsed_metadata.cc",
        "src/core/lib/transport/status_conversion.cc",
        "src/core/lib/transport/timeout_encoding.cc",
        "src/core/lib/transport/transport.cc",
        "src/core/lib/transport/transport_op_string.cc",
    ],
    hdrs = [
        "src/core/lib/transport/error_utils.h",
        "src/core/lib/address_utils/parse_address.h",
        "src/core/lib/channel/call_finalization.h",
        "src/core/lib/channel/call_tracer.h",
        "src/core/lib/channel/channel_stack.h",
        "src/core/lib/channel/promise_based_filter.h",
        "src/core/lib/channel/channel_stack_builder_impl.h",
        "src/core/lib/channel/channel_trace.h",
        "src/core/lib/channel/channelz.h",
        "src/core/lib/channel/channelz_registry.h",
        "src/core/lib/channel/connected_channel.h",
        "src/core/lib/channel/context.h",
        "src/core/lib/channel/status_util.h",
        "src/core/lib/compression/compression_internal.h",
        "src/core/lib/resource_quota/api.h",
        "src/core/lib/compression/message_compress.h",
        "src/core/lib/debug/stats.h",
        "src/core/lib/debug/stats_data.h",
        "src/core/lib/event_engine/channel_args_endpoint_config.h",
        "src/core/lib/iomgr/block_annotate.h",
        "src/core/lib/iomgr/buffer_list.h",
        "src/core/lib/iomgr/call_combiner.h",
        "src/core/lib/iomgr/cfstream_handle.h",
        "src/core/lib/iomgr/dynamic_annotations.h",
        "src/core/lib/iomgr/endpoint.h",
        "src/core/lib/iomgr/endpoint_cfstream.h",
        "src/core/lib/iomgr/endpoint_pair.h",
        "src/core/lib/iomgr/error_cfstream.h",
        "src/core/lib/iomgr/ev_apple.h",
        "src/core/lib/iomgr/ev_epoll1_linux.h",
        "src/core/lib/iomgr/ev_poll_posix.h",
        "src/core/lib/iomgr/ev_posix.h",
        "src/core/lib/iomgr/gethostname.h",
        "src/core/lib/iomgr/grpc_if_nametoindex.h",
        "src/core/lib/iomgr/internal_errqueue.h",
        "src/core/lib/iomgr/iocp_windows.h",
        "src/core/lib/iomgr/iomgr.h",
        "src/core/lib/iomgr/load_file.h",
        "src/core/lib/iomgr/lockfree_event.h",
        "src/core/lib/iomgr/nameser.h",
        "src/core/lib/iomgr/polling_entity.h",
        "src/core/lib/iomgr/pollset.h",
        "src/core/lib/iomgr/pollset_set_windows.h",
        "src/core/lib/iomgr/pollset_windows.h",
        "src/core/lib/iomgr/python_util.h",
        "src/core/lib/iomgr/resolve_address.h",
        "src/core/lib/iomgr/resolve_address_impl.h",
        "src/core/lib/iomgr/resolve_address_posix.h",
        "src/core/lib/iomgr/resolve_address_windows.h",
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_posix.h",
        "src/core/lib/iomgr/sockaddr_windows.h",
        "src/core/lib/iomgr/socket_factory_posix.h",
        "src/core/lib/iomgr/socket_mutator.h",
        "src/core/lib/iomgr/socket_utils_posix.h",
        "src/core/lib/iomgr/socket_windows.h",
        "src/core/lib/iomgr/tcp_client.h",
        "src/core/lib/iomgr/tcp_client_posix.h",
        "src/core/lib/iomgr/tcp_posix.h",
        "src/core/lib/iomgr/tcp_server.h",
        "src/core/lib/iomgr/tcp_server_utils_posix.h",
        "src/core/lib/iomgr/tcp_windows.h",
        "src/core/lib/iomgr/unix_sockets_posix.h",
        "src/core/lib/iomgr/wakeup_fd_pipe.h",
        "src/core/lib/iomgr/wakeup_fd_posix.h",
        "src/core/lib/slice/b64.h",
        "src/core/lib/surface/api_trace.h",
        "src/core/lib/surface/builtins.h",
        "src/core/lib/surface/call.h",
        "src/core/lib/surface/call_test_only.h",
        "src/core/lib/surface/channel.h",
        "src/core/lib/surface/completion_queue.h",
        "src/core/lib/surface/completion_queue_factory.h",
        "src/core/lib/surface/event_string.h",
        "src/core/lib/surface/init.h",
        "src/core/lib/surface/lame_client.h",
        "src/core/lib/surface/server.h",
        "src/core/lib/surface/validate_metadata.h",
        "src/core/lib/transport/connectivity_state.h",
        "src/core/lib/transport/metadata_batch.h",
        "src/core/lib/transport/parsed_metadata.h",
        "src/core/lib/transport/status_conversion.h",
        "src/core/lib/transport/timeout_encoding.h",
        "src/core/lib/transport/transport.h",
        "src/core/lib/transport/transport_impl.h",
    ] +
    # TODO(ctiller): remove these
    # These headers used to be vended by this target, but they have been split
    # out into separate targets now. In order to transition downstream code, we
    # re-export these headers from here for now, and when LSC's have completed
    # to clean this up, we'll remove these.
    [
        "src/core/lib/iomgr/closure.h",
        "src/core/lib/iomgr/error.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/iomgr_internal.h",
        "src/core/lib/channel/channel_args.h",
        "src/core/lib/channel/channel_stack_builder.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/memory",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/time",
        "absl/types:optional",
        "absl/types:variant",
        "absl/utility",
        "madler_zlib",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    tags = ["nofixdeps"],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "activity",
        "arena",
        "arena_promise",
        "atomic_utils",
        "avl",
        "bitset",
        "channel_args",
        "channel_args_preconditioning",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "chunked_vector",
        "closure",
        "config",
        "context",
        "cpp_impl_of",
        "debug_location",
        "default_event_engine",
        "dual_ref_counted",
        "error",
        "event_engine_common",
        "exec_ctx",
        "experiments",
        "gpr",
        "gpr_atm",
        "gpr_manual_constructor",
        "gpr_murmur_hash",
        "gpr_spinlock",
        "gpr_tls",
        "grpc_public_hdrs",
        "grpc_sockaddr",
        "grpc_trace",
        "handshaker_registry",
        "http2_errors",
        "init_internally",
        "iomgr_fwd",
        "iomgr_port",
        "iomgr_timer",
        "json",
        "latch",
        "memory_quota",
        "notification",
        "orphanable",
        "packed_table",
        "poll",
        "pollset_set",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "resource_quota_trace",
        "slice",
        "slice_buffer",
        "slice_refcount",
        "sockaddr_utils",
        "status_helper",
        "table",
        "thread_quota",
        "time",
        "transport_fwd",
        "uri_parser",
        "useful",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "http2_errors",
    hdrs = [
        "src/core/lib/transport/http2_errors.h",
    ],
)

grpc_cc_library(
    name = "channel_stack_type",
    srcs = [
        "src/core/lib/surface/channel_stack_type.cc",
    ],
    hdrs = [
        "src/core/lib/surface/channel_stack_type.h",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "channel_init",
    srcs = [
        "src/core/lib/surface/channel_init.cc",
    ],
    hdrs = [
        "src/core/lib/surface/channel_init.h",
    ],
    language = "c++",
    deps = [
        "channel_stack_builder",
        "channel_stack_type",
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "single_set_ptr",
    hdrs = [
        "src/core/lib/gprpp/single_set_ptr.h",
    ],
    language = "c++",
    deps = ["gpr"],
)

grpc_cc_library(
    name = "channel_stack_builder",
    srcs = [
        "src/core/lib/channel/channel_stack_builder.cc",
    ],
    hdrs = [
        "src/core/lib/channel/channel_stack_builder.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "gpr",
        "ref_counted_ptr",
        "transport_fwd",
    ],
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
            "//conditions:default": ["grpc_lb_policy_rls"],
        },
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_base",
        # standard plugins
        "census",
        "grpc_deadline_filter",
        "grpc_client_authority_filter",
        "grpc_lb_policy_grpclb",
        "grpc_lb_policy_outlier_detection",
        "grpc_lb_policy_pick_first",
        "grpc_lb_policy_priority",
        "grpc_lb_policy_ring_hash",
        "grpc_lb_policy_round_robin",
        "grpc_lb_policy_weighted_target",
        "grpc_channel_idle_filter",
        "grpc_message_size_filter",
        "grpc_resolver_binder",
        "grpc_resolver_dns_ares",
        "grpc_resolver_fake",
        "grpc_resolver_dns_native",
        "grpc_resolver_sockaddr",
        "grpc_transport_chttp2_client_connector",
        "grpc_transport_chttp2_server",
        "grpc_transport_inproc",
        "grpc_fault_injection_filter",
    ],
)

grpc_cc_library(
    name = "grpc_service_config",
    hdrs = [
        "src/core/lib/service_config/service_config.h",
        "src/core/lib/service_config/service_config_call_data.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "gpr_platform",
        "ref_counted",
        "ref_counted_ptr",
        "service_config_parser",
        "slice_refcount",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_service_config_impl",
    srcs = [
        "src/core/lib/service_config/service_config_impl.cc",
    ],
    hdrs = [
        "src/core/lib/service_config/service_config_impl.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "channel_args",
        "config",
        "gpr",
        "grpc_service_config",
        "json",
        "ref_counted_ptr",
        "service_config_parser",
        "slice",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "service_config_parser",
    srcs = [
        "src/core/lib/service_config/service_config_parser.cc",
    ],
    hdrs = [
        "src/core/lib/service_config/service_config_parser.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "gpr",
        "json",
    ],
)

grpc_cc_library(
    name = "server_address",
    srcs = [
        "src/core/lib/resolver/server_address.cc",
    ],
    hdrs = [
        "src/core/lib/resolver/server_address.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "channel_args",
        "gpr_platform",
        "resolved_address",
        "sockaddr_utils",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_resolver",
    srcs = [
        "src/core/lib/resolver/resolver.cc",
        "src/core/lib/resolver/resolver_registry.cc",
    ],
    hdrs = [
        "src/core/lib/resolver/resolver.h",
        "src/core/lib/resolver/resolver_factory.h",
        "src/core/lib/resolver/resolver_registry.h",
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
        "channel_args",
        "gpr",
        "grpc_service_config",
        "grpc_trace",
        "iomgr_fwd",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "notification",
    hdrs = [
        "src/core/lib/gprpp/notification.h",
    ],
    external_deps = ["absl/time"],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "channel_args",
    srcs = [
        "src/core/lib/channel/channel_args.cc",
    ],
    hdrs = [
        "src/core/lib/channel/channel_args.h",
    ],
    external_deps = [
        "absl/meta:type_traits",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "avl",
        "channel_stack_type",
        "debug_location",
        "dual_ref_counted",
        "gpr",
        "grpc_codegen",
        "match",
        "ref_counted",
        "ref_counted_ptr",
        "time",
        "useful",
    ],
)

grpc_cc_library(
    name = "resolved_address",
    hdrs = ["src/core/lib/iomgr/resolved_address.h"],
    language = "c++",
    deps = [
        "gpr_platform",
        "iomgr_port",
    ],
)

grpc_cc_library(
    name = "lb_policy",
    srcs = ["src/core/lib/load_balancing/lb_policy.cc"],
    hdrs = ["src/core/lib/load_balancing/lb_policy.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    deps = [
        "channel_args",
        "closure",
        "debug_location",
        "error",
        "exec_ctx",
        "gpr_platform",
        "grpc_backend_metric_data",
        "grpc_codegen",
        "grpc_trace",
        "iomgr_fwd",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "lb_policy_factory",
    hdrs = ["src/core/lib/load_balancing/lb_policy_factory.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "gpr_platform",
        "json",
        "lb_policy",
        "orphanable",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "lb_policy_registry",
    srcs = ["src/core/lib/load_balancing/lb_policy_registry.cc"],
    hdrs = ["src/core/lib/load_balancing/lb_policy_registry.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "gpr",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "orphanable",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "subchannel_interface",
    hdrs = ["src/core/lib/load_balancing/subchannel_interface.h"],
    external_deps = ["absl/status"],
    deps = [
        "channel_args",
        "gpr_platform",
        "grpc_codegen",
        "iomgr_fwd",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "proxy_mapper",
    hdrs = ["src/core/lib/handshaker/proxy_mapper.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "gpr_platform",
        "resolved_address",
    ],
)

grpc_cc_library(
    name = "proxy_mapper_registry",
    srcs = ["src/core/lib/handshaker/proxy_mapper_registry.cc"],
    hdrs = ["src/core/lib/handshaker/proxy_mapper_registry.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "gpr_platform",
        "proxy_mapper",
        "resolved_address",
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
        "src/core/ext/filters/client_channel/http_proxy.cc",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc",
        "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.cc",
        "src/core/ext/filters/client_channel/local_subchannel_pool.cc",
        "src/core/ext/filters/client_channel/resolver_result_parsing.cc",
        "src/core/ext/filters/client_channel/retry_filter.cc",
        "src/core/ext/filters/client_channel/retry_service_config.cc",
        "src/core/ext/filters/client_channel/retry_throttle.cc",
        "src/core/ext/filters/client_channel/service_config_channel_arg_filter.cc",
        "src/core/ext/filters/client_channel/subchannel.cc",
        "src/core/ext/filters/client_channel/subchannel_pool_interface.cc",
        "src/core/ext/filters/client_channel/subchannel_stream_client.cc",
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
        "src/core/ext/filters/client_channel/http_proxy.h",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h",
        "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h",
        "src/core/ext/filters/client_channel/local_subchannel_pool.h",
        "src/core/ext/filters/client_channel/resolver_result_parsing.h",
        "src/core/ext/filters/client_channel/retry_filter.h",
        "src/core/ext/filters/client_channel/retry_service_config.h",
        "src/core/ext/filters/client_channel/retry_throttle.h",
        "src/core/ext/filters/client_channel/subchannel.h",
        "src/core/ext/filters/client_channel/subchannel_interface_internal.h",
        "src/core/ext/filters/client_channel/subchannel_pool_interface.h",
        "src/core/ext/filters/client_channel/subchannel_stream_client.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:cord",
        "absl/types:optional",
        "absl/types:variant",
        "upb_lib",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "arena",
        "backoff",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "config",
        "construct_destruct",
        "debug_location",
        "default_event_engine",
        "dual_ref_counted",
        "env",
        "gpr",
        "gpr_atm",
        "grpc_backend_metric_data",
        "grpc_base",
        "grpc_codegen",
        "grpc_deadline_filter",
        "grpc_health_upb",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "http_connect_handshaker",
        "init_internally",
        "iomgr_fwd",
        "iomgr_timer",
        "json",
        "json_util",
        "lb_policy",
        "lb_policy_registry",
        "memory_quota",
        "orphanable",
        "pollset_set",
        "protobuf_duration_upb",
        "proxy_mapper",
        "proxy_mapper_registry",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "server_address",
        "service_config_parser",
        "slice",
        "slice_buffer",
        "slice_refcount",
        "sockaddr_utils",
        "subchannel_interface",
        "time",
        "transport_fwd",
        "unique_type_name",
        "uri_parser",
        "useful",
        "work_serializer",
        "xds_orca_service_upb",
        "xds_orca_upb",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector",
    srcs = [
        "src/core/ext/filters/server_config_selector/server_config_selector.cc",
    ],
    hdrs = [
        "src/core/ext/filters/server_config_selector/server_config_selector.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "dual_ref_counted",
        "gpr_platform",
        "grpc_base",
        "grpc_codegen",
        "grpc_service_config",
        "ref_counted",
        "ref_counted_ptr",
        "service_config_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector_filter",
    srcs = [
        "src/core/ext/filters/server_config_selector/server_config_selector_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/server_config_selector/server_config_selector_filter.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "gpr",
        "grpc_base",
        "grpc_server_config_selector",
        "grpc_service_config",
        "promise",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "sorted_pack",
    hdrs = [
        "src/core/lib/gprpp/sorted_pack.h",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "idle_filter_state",
    srcs = [
        "src/core/ext/filters/channel_idle/idle_filter_state.cc",
    ],
    hdrs = [
        "src/core/ext/filters/channel_idle/idle_filter_state.h",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_channel_idle_filter",
    srcs = [
        "src/core/ext/filters/channel_idle/channel_idle_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/channel_idle/channel_idle_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    deps = [
        "activity",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "closure",
        "config",
        "debug_location",
        "exec_ctx",
        "exec_ctx_wakeup_scheduler",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_trace",
        "http2_errors",
        "idle_filter_state",
        "loop",
        "orphanable",
        "poll",
        "promise",
        "ref_counted_ptr",
        "single_set_ptr",
        "sleep",
        "time",
        "try_seq",
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
    external_deps = [
        "absl/status",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "closure",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "iomgr_timer",
        "time",
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
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "config",
        "gpr_platform",
        "grpc_base",
        "grpc_codegen",
        "slice",
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
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "closure",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_service_config",
        "json",
        "service_config_parser",
        "slice_buffer",
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
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_fwd",
        "config",
        "context",
        "gpr",
        "grpc_base",
        "grpc_public_hdrs",
        "grpc_service_config",
        "grpc_trace",
        "json",
        "json_util",
        "service_config_parser",
        "sleep",
        "time",
        "try_seq",
    ],
)

grpc_cc_library(
    name = "grpc_rbac_filter",
    srcs = [
        "src/core/ext/filters/rbac/rbac_filter.cc",
        "src/core/ext/filters/rbac/rbac_service_config_parser.cc",
    ],
    hdrs = [
        "src/core/ext/filters/rbac/rbac_filter.h",
        "src/core/ext/filters/rbac/rbac_service_config_parser.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_fwd",
        "closure",
        "config",
        "debug_location",
        "gpr",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_matchers",
        "grpc_public_hdrs",
        "grpc_rbac_engine",
        "grpc_security_base",
        "grpc_service_config",
        "json",
        "json_util",
        "service_config_parser",
        "transport_fwd",
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
        "arena",
        "arena_promise",
        "basic_seq",
        "call_push_pull",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "config",
        "context",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_message_size_filter",
        "grpc_public_hdrs",
        "grpc_trace",
        "latch",
        "percent_encoding",
        "promise",
        "seq",
        "slice",
        "slice_buffer",
        "transport_fwd",
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
    visibility = ["@grpc:public"],
    deps = ["gpr"],
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
    visibility = ["@grpc:grpclb"],
    deps = [
        "channel_args",
        "gpr_platform",
        "grpc_codegen",
        "server_address",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "backoff",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "config",
        "debug_location",
        "default_event_engine",
        "gpr",
        "gpr_atm",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_grpclb_balancer_addresses",
        "grpc_lb_upb",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_resolver_fake",
        "grpc_security_base",
        "grpc_sockaddr",
        "grpc_trace",
        "iomgr_timer",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "protobuf_duration_upb",
        "protobuf_timestamp_upb",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "server_address",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "subchannel_interface",
        "time",
        "uri_parser",
        "useful",
        "validation_errors",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_backend_metric_data",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_lb_policy_rls",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/rls/rls.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/hash",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "backoff",
        "config",
        "debug_location",
        "dual_ref_counted",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_fake_credentials",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_service_config_impl",
        "grpc_trace",
        "iomgr_timer",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "rls_upb",
        "server_address",
        "slice_refcount",
        "subchannel_interface",
        "time",
        "uri_parser",
        "validation_errors",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "upb_utils",
    hdrs = [
        "src/core/ext/xds/upb_utils.h",
    ],
    external_deps = [
        "absl/strings",
        "upb_lib",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "xds_client",
    srcs = [
        "src/core/ext/xds/xds_api.cc",
        "src/core/ext/xds/xds_bootstrap.cc",
        "src/core/ext/xds/xds_client.cc",
        "src/core/ext/xds/xds_client_stats.cc",
        "src/core/ext/xds/xds_resource_type.cc",
    ],
    hdrs = [
        "src/core/ext/xds/xds_api.h",
        "src/core/ext/xds/xds_bootstrap.h",
        "src/core/ext/xds/xds_channel_args.h",
        "src/core/ext/xds/xds_client.h",
        "src/core/ext/xds/xds_client_stats.h",
        "src/core/ext/xds/xds_resource_type.h",
        "src/core/ext/xds/xds_resource_type_impl.h",
        "src/core/ext/xds/xds_transport.h",
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
        "default_event_engine",
        "dual_ref_counted",
        "env",
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
        "json",
        "orphanable",
        "protobuf_any_upb",
        "protobuf_duration_upb",
        "protobuf_struct_upb",
        "protobuf_timestamp_upb",
        "ref_counted",
        "ref_counted_ptr",
        "time",
        "upb_utils",
        "uri_parser",
        "useful",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "certificate_provider_factory",
    hdrs = [
        "src/core/lib/security/certificate_provider/certificate_provider_factory.h",
    ],
    deps = [
        "error",
        "gpr",
        "grpc_public_hdrs",
        "json",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "certificate_provider_registry",
    srcs = [
        "src/core/lib/security/certificate_provider/certificate_provider_registry.cc",
    ],
    hdrs = [
        "src/core/lib/security/certificate_provider/certificate_provider_registry.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "certificate_provider_factory",
        "gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "grpc_xds_client",
    srcs = [
        "src/core/ext/xds/certificate_provider_store.cc",
        "src/core/ext/xds/file_watcher_certificate_provider_factory.cc",
        "src/core/ext/xds/xds_bootstrap_grpc.cc",
        "src/core/ext/xds/xds_certificate_provider.cc",
        "src/core/ext/xds/xds_client_grpc.cc",
        "src/core/ext/xds/xds_cluster.cc",
        "src/core/ext/xds/xds_cluster_specifier_plugin.cc",
        "src/core/ext/xds/xds_common_types.cc",
        "src/core/ext/xds/xds_endpoint.cc",
        "src/core/ext/xds/xds_http_fault_filter.cc",
        "src/core/ext/xds/xds_http_filters.cc",
        "src/core/ext/xds/xds_http_rbac_filter.cc",
        "src/core/ext/xds/xds_lb_policy_registry.cc",
        "src/core/ext/xds/xds_listener.cc",
        "src/core/ext/xds/xds_route_config.cc",
        "src/core/ext/xds/xds_routing.cc",
        "src/core/ext/xds/xds_transport_grpc.cc",
        "src/core/lib/security/credentials/xds/xds_credentials.cc",
    ],
    hdrs = [
        "src/core/ext/xds/certificate_provider_store.h",
        "src/core/ext/xds/file_watcher_certificate_provider_factory.h",
        "src/core/ext/xds/xds_bootstrap_grpc.h",
        "src/core/ext/xds/xds_certificate_provider.h",
        "src/core/ext/xds/xds_client_grpc.h",
        "src/core/ext/xds/xds_cluster.h",
        "src/core/ext/xds/xds_cluster_specifier_plugin.h",
        "src/core/ext/xds/xds_common_types.h",
        "src/core/ext/xds/xds_endpoint.h",
        "src/core/ext/xds/xds_http_fault_filter.h",
        "src/core/ext/xds/xds_http_filters.h",
        "src/core/ext/xds/xds_http_rbac_filter.h",
        "src/core/ext/xds/xds_lb_policy_registry.h",
        "src/core/ext/xds/xds_listener.h",
        "src/core/ext/xds/xds_route_config.h",
        "src/core/ext/xds/xds_routing.h",
        "src/core/ext/xds/xds_transport_grpc.h",
        "src/core/lib/security/credentials/xds/xds_credentials.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "upb_lib",
        "upb_textformat_lib",
        "upb_json_lib",
        "re2",
        "upb_reflection",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "certificate_provider_factory",
        "certificate_provider_registry",
        "channel_creds_registry",
        "channel_fwd",
        "config",
        "debug_location",
        "default_event_engine",
        "env",
        "envoy_admin_upb",
        "envoy_config_cluster_upb",
        "envoy_config_cluster_upbdefs",
        "envoy_config_core_upb",
        "envoy_config_endpoint_upb",
        "envoy_config_endpoint_upbdefs",
        "envoy_config_listener_upb",
        "envoy_config_listener_upbdefs",
        "envoy_config_rbac_upb",
        "envoy_config_route_upb",
        "envoy_config_route_upbdefs",
        "envoy_extensions_clusters_aggregate_upb",
        "envoy_extensions_clusters_aggregate_upbdefs",
        "envoy_extensions_filters_common_fault_upb",
        "envoy_extensions_filters_http_fault_upb",
        "envoy_extensions_filters_http_fault_upbdefs",
        "envoy_extensions_filters_http_rbac_upb",
        "envoy_extensions_filters_http_rbac_upbdefs",
        "envoy_extensions_filters_http_router_upb",
        "envoy_extensions_filters_http_router_upbdefs",
        "envoy_extensions_filters_network_http_connection_manager_upb",
        "envoy_extensions_filters_network_http_connection_manager_upbdefs",
        "envoy_extensions_load_balancing_policies_ring_hash_upb",
        "envoy_extensions_load_balancing_policies_wrr_locality_upb",
        "envoy_extensions_transport_sockets_tls_upb",
        "envoy_extensions_transport_sockets_tls_upbdefs",
        "envoy_service_discovery_upb",
        "envoy_service_discovery_upbdefs",
        "envoy_service_load_stats_upb",
        "envoy_service_load_stats_upbdefs",
        "envoy_service_status_upb",
        "envoy_service_status_upbdefs",
        "envoy_type_matcher_upb",
        "envoy_type_upb",
        "error",
        "google_rpc_status_upb",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_fake_credentials",
        "grpc_fault_injection_filter",
        "grpc_lb_xds_channel_args",
        "grpc_matchers",
        "grpc_outlier_detection_header",
        "grpc_rbac_filter",
        "grpc_security_base",
        "grpc_sockaddr",
        "grpc_tls_credentials",
        "grpc_trace",
        "grpc_transport_chttp2_client_connector",
        "init_internally",
        "iomgr_fwd",
        "iomgr_timer",
        "json",
        "json_object_loader",
        "json_util",
        "lb_policy_registry",
        "match",
        "orphanable",
        "pollset_set",
        "protobuf_any_upb",
        "protobuf_duration_upb",
        "protobuf_struct_upb",
        "protobuf_struct_upbdefs",
        "protobuf_timestamp_upb",
        "protobuf_wrappers_upb",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "rls_config_upb",
        "rls_config_upbdefs",
        "server_address",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "status_helper",
        "time",
        "tsi_ssl_credentials",
        "unique_type_name",
        "upb_utils",
        "uri_parser",
        "useful",
        "work_serializer",
        "xds_client",
        "xds_type_upb",
        "xds_type_upbdefs",
    ],
)

grpc_cc_library(
    name = "grpc_xds_channel_stack_modifier",
    srcs = [
        "src/core/ext/xds/xds_channel_stack_modifier.cc",
    ],
    hdrs = [
        "src/core/ext/xds/xds_channel_stack_modifier.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_builder",
        "channel_stack_type",
        "config",
        "gpr_platform",
        "grpc_base",
        "grpc_codegen",
        "ref_counted",
        "ref_counted_ptr",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_xds_server_config_fetcher",
    srcs = [
        "src/core/ext/xds/xds_server_config_fetcher.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_args_preconditioning",
        "channel_fwd",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_server_config_selector",
        "grpc_server_config_selector_filter",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_sockaddr",
        "grpc_tls_credentials",
        "grpc_trace",
        "grpc_xds_channel_stack_modifier",
        "grpc_xds_client",
        "iomgr_fwd",
        "ref_counted_ptr",
        "resolved_address",
        "slice_refcount",
        "sockaddr_utils",
        "unique_type_name",
        "uri_parser",
        "xds_client",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry_init",
    srcs = [
        "src/core/lib/security/credentials/channel_creds_registry_init.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_creds_registry",
        "config",
        "gpr_platform",
        "grpc_fake_credentials",
        "grpc_google_default_credentials",
        "grpc_security_base",
        "json",
        "ref_counted_ptr",
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
        "certificate_provider_factory",
        "error",
        "gpr_platform",
        "grpc_tls_credentials",
        "grpc_trace",
        "json",
        "json_util",
        "ref_counted_ptr",
        "time",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_cds",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/cds.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_matchers",
        "grpc_outlier_detection_header",
        "grpc_security_base",
        "grpc_tls_credentials",
        "grpc_trace",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "time",
        "unique_type_name",
        "work_serializer",
        "xds_client",
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
    external_deps = ["absl/memory"],
    language = "c++",
    deps = [
        "gpr_platform",
        "ref_counted_ptr",
        "server_address",
        "xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_resolver",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_resolver.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_address_filtering",
        "grpc_lb_policy_ring_hash",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_outlier_detection_header",
        "grpc_resolver",
        "grpc_resolver_fake",
        "grpc_trace",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "validation_errors",
        "work_serializer",
        "xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_impl",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_impl.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_trace",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "validation_errors",
        "xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_manager",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_manager.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_resolver_xds_header",
        "grpc_trace",
        "iomgr_timer",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "time",
        "validation_errors",
        "work_serializer",
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
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_platform",
        "server_address",
    ],
)

grpc_cc_library(
    name = "grpc_lb_subchannel_list",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h",
    ],
    external_deps = [
        "absl/status",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "debug_location",
        "dual_ref_counted",
        "gpr",
        "gpr_manual_constructor",
        "grpc_base",
        "grpc_codegen",
        "iomgr_fwd",
        "lb_policy",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_pick_first",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_lb_subchannel_list",
        "grpc_trace",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
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
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "xxhash",
    ],
    language = "c++",
    deps = [
        "closure",
        "config",
        "debug_location",
        "exec_ctx",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_subchannel_list",
        "grpc_trace",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "sockaddr_utils",
        "subchannel_interface",
        "unique_type_name",
        "validation_errors",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_lb_subchannel_list",
        "grpc_trace",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
    ],
)

grpc_cc_library(
    name = "grpc_outlier_detection_header",
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h",
    ],
    external_deps = ["absl/types:optional"],
    language = "c++",
    deps = [
        "gpr_platform",
        "json",
        "json_args",
        "json_object_loader",
        "time",
        "validation_errors",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_outlier_detection",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "config",
        "debug_location",
        "env",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_outlier_detection_header",
        "grpc_trace",
        "iomgr_fwd",
        "iomgr_timer",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "sockaddr_utils",
        "subchannel_interface",
        "validation_errors",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_priority",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/priority/priority.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_address_filtering",
        "grpc_trace",
        "iomgr_timer",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "time",
        "validation_errors",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_target",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "config",
        "debug_location",
        "default_event_engine",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_address_filtering",
        "grpc_trace",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "orphanable",
        "pollset_set",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "subchannel_interface",
        "time",
        "validation_errors",
        "work_serializer",
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
        "absl/container:inlined_vector",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "opencensus-stats",
        "opencensus-tags",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "config",
        "context",
        "gpr",
        "gpr_platform",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_sockaddr",
        "promise",
        "resolved_address",
        "seq",
        "slice",
        "uri_parser",
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
        "gpr",
        "gpr_platform",
        "grpc++",
        "grpc_sockaddr",
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
        "gpr",
        "gpr_platform",
        "grpc",
        "grpc++",
        "grpc++_codegen_base",
        "grpc_codegen",
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
    name = "polling_resolver",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/polling_resolver.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/polling_resolver.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "backoff",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_resolver",
        "grpc_service_config",
        "grpc_trace",
        "iomgr_fwd",
        "iomgr_timer",
        "orphanable",
        "ref_counted_ptr",
        "time",
        "uri_parser",
        "work_serializer",
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
    deps = ["gpr"],
)

grpc_cc_library(
    name = "grpc_resolver_dns_native",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc",
    ],
    external_deps = [
        "absl/functional:bind_front",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "backoff",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_resolver",
        "grpc_resolver_dns_selection",
        "grpc_trace",
        "orphanable",
        "polling_resolver",
        "ref_counted_ptr",
        "resolved_address",
        "server_address",
        "time",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/memory",
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
        "config",
        "debug_location",
        "event_engine_common",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_grpclb_balancer_addresses",
        "grpc_resolver",
        "grpc_resolver_dns_selection",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_sockaddr",
        "grpc_trace",
        "iomgr_fwd",
        "iomgr_port",
        "iomgr_timer",
        "json",
        "orphanable",
        "polling_resolver",
        "pollset_set",
        "ref_counted_ptr",
        "resolved_address",
        "server_address",
        "sockaddr_utils",
        "time",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr",
        "grpc_base",
        "grpc_resolver",
        "iomgr_port",
        "orphanable",
        "resolved_address",
        "server_address",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_binder",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/binder/binder_resolver.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr",
        "grpc_base",
        "grpc_resolver",
        "iomgr_port",
        "orphanable",
        "resolved_address",
        "server_address",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc"],
    hdrs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
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
        "channel_args",
        "config",
        "debug_location",
        "gpr",
        "grpc_codegen",
        "grpc_resolver",
        "grpc_service_config",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "uri_parser",
        "useful",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds_header",
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/xds/xds_resolver.h",
    ],
    language = "c++",
    deps = [
        "gpr_platform",
        "unique_type_name",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/xds/xds_resolver.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "re2",
        "xxhash",
    ],
    language = "c++",
    deps = [
        "arena",
        "channel_fwd",
        "config",
        "debug_location",
        "dual_ref_counted",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_lb_policy_ring_hash",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_xds_client",
        "iomgr_fwd",
        "match",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "server_address",
        "time",
        "unique_type_name",
        "uri_parser",
        "work_serializer",
        "xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_c2p",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/google_c2p/google_c2p_resolver.cc",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "alts_util",
        "config",
        "debug_location",
        "env",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_xds_client",
        "httpcli",
        "json",
        "orphanable",
        "ref_counted_ptr",
        "resource_quota",
        "time",
        "uri_parser",
        "work_serializer",
    ],
)

grpc_cc_library(
    name = "httpcli",
    srcs = [
        "src/core/lib/http/format_request.cc",
        "src/core/lib/http/httpcli.cc",
        "src/core/lib/http/parser.cc",
    ],
    hdrs = [
        "src/core/lib/http/format_request.h",
        "src/core/lib/http/httpcli.h",
        "src/core/lib/http/parser.h",
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
        "channel_args_preconditioning",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_security_base",
        "grpc_trace",
        "handshaker",
        "handshaker_registry",
        "iomgr_fwd",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "slice_refcount",
        "sockaddr_utils",
        "tcp_connect_handshaker",
        "time",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_authorization_base",
    srcs = [
        "src/core/lib/security/authorization/authorization_policy_provider_vtable.cc",
        "src/core/lib/security/authorization/evaluate_args.cc",
        "src/core/lib/security/authorization/grpc_server_authz_filter.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/authorization_engine.h",
        "src/core/lib/security/authorization/authorization_policy_provider.h",
        "src/core/lib/security/authorization/evaluate_args.h",
        "src/core/lib/security/authorization/grpc_server_authz_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "dual_ref_counted",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_trace",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "slice",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "tsi_fake_credentials",
    srcs = [
        "src/core/tsi/fake_transport_security.cc",
    ],
    hdrs = [
        "src/core/tsi/fake_transport_security.h",
    ],
    language = "c++",
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "gpr",
        "slice",
        "tsi_base",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_fake_credentials",
    srcs = [
        "src/core/lib/security/credentials/fake/fake_credentials.cc",
        "src/core/lib/security/security_connector/fake/fake_security_connector.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/lib/security/credentials/fake/fake_credentials.h",
        "src/core/lib/security/security_connector/fake/fake_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_security_base",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "slice",
        "tsi_base",
        "tsi_fake_credentials",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_insecure_credentials",
    srcs = [
        "src/core/lib/security/credentials/insecure/insecure_credentials.cc",
        "src/core/lib/security/security_connector/insecure/insecure_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/insecure/insecure_credentials.h",
        "src/core/lib/security/security_connector/insecure/insecure_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_local_credentials",
        "unique_type_name",
    ],
)

grpc_cc_library(
    name = "tsi_local_credentials",
    srcs = [
        "src/core/tsi/local_transport_security.cc",
    ],
    hdrs = [
        "src/core/tsi/local_transport_security.h",
    ],
    language = "c++",
    deps = [
        "exec_ctx",
        "gpr",
        "grpc_trace",
        "tsi_base",
    ],
)

grpc_cc_library(
    name = "grpc_local_credentials",
    srcs = [
        "src/core/lib/security/credentials/local/local_credentials.cc",
        "src/core/lib/security/security_connector/local/local_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/local/local_credentials.h",
        "src/core/lib/security/security_connector/local/local_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_security_base",
        "grpc_sockaddr",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "resolved_address",
        "sockaddr_utils",
        "tsi_base",
        "tsi_local_credentials",
        "unique_type_name",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_alts_credentials",
    srcs = [
        "src/core/lib/security/credentials/alts/alts_credentials.cc",
        "src/core/lib/security/security_connector/alts/alts_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/alts/alts_credentials.h",
        "src/core/lib/security/security_connector/alts/alts_security_connector.h",
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
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_security_base",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "slice_refcount",
        "tsi_alts_credentials",
        "tsi_base",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_ssl_credentials",
    srcs = [
        "src/core/lib/security/credentials/ssl/ssl_credentials.cc",
        "src/core/lib/security/security_connector/ssl/ssl_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/ssl/ssl_credentials.h",
        "src/core/lib/security/security_connector/ssl/ssl_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_security_base",
        "grpc_trace",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_ssl_credentials",
        "tsi_ssl_session_cache",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_google_default_credentials",
    srcs = [
        "src/core/lib/security/credentials/google_default/credentials_generic.cc",
        "src/core/lib/security/credentials/google_default/google_default_credentials.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "src/core/lib/security/credentials/google_default/google_default_credentials.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "alts_util",
        "env",
        "gpr",
        "grpc_alts_credentials",
        "grpc_base",
        "grpc_codegen",
        "grpc_external_account_credentials",
        "grpc_jwt_credentials",
        "grpc_lb_xds_channel_args",
        "grpc_oauth2_credentials",
        "grpc_security_base",
        "grpc_ssl_credentials",
        "grpc_trace",
        "httpcli",
        "iomgr_fwd",
        "json",
        "ref_counted_ptr",
        "slice_refcount",
        "time",
        "unique_type_name",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_tls_credentials",
    srcs = [
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.cc",
        "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.cc",
        "src/core/lib/security/credentials/tls/tls_credentials.cc",
        "src/core/lib/security/security_connector/tls/tls_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h",
        "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h",
        "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h",
        "src/core/lib/security/credentials/tls/tls_credentials.h",
        "src/core/lib/security/security_connector/tls/tls_security_connector.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:bind_front",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_public_hdrs",
        "grpc_security_base",
        "grpc_trace",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "slice_refcount",
        "tsi_base",
        "tsi_ssl_credentials",
        "tsi_ssl_session_cache",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_iam_credentials",
    srcs = [
        "src/core/lib/security/credentials/iam/iam_credentials.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/iam/iam_credentials.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "grpc_trace",
        "promise",
        "ref_counted_ptr",
        "slice",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_jwt_credentials",
    srcs = [
        "src/core/lib/security/credentials/jwt/json_token.cc",
        "src/core/lib/security/credentials/jwt/jwt_credentials.cc",
        "src/core/lib/security/credentials/jwt/jwt_verifier.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/jwt/json_token.h",
        "src/core/lib/security/credentials/jwt/jwt_credentials.h",
        "src/core/lib/security/credentials/jwt/jwt_verifier.h",
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
        "arena_promise",
        "gpr",
        "gpr_manual_constructor",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_trace",
        "httpcli",
        "httpcli_ssl_credentials",
        "iomgr_fwd",
        "json",
        "orphanable",
        "promise",
        "ref_counted_ptr",
        "slice",
        "slice_refcount",
        "time",
        "tsi_ssl_types",
        "unique_type_name",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_oauth2_credentials",
    srcs = [
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/oauth2/oauth2_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "activity",
        "arena_promise",
        "context",
        "gpr",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_trace",
        "httpcli",
        "httpcli_ssl_credentials",
        "json",
        "orphanable",
        "poll",
        "pollset_set",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "slice",
        "slice_refcount",
        "time",
        "unique_type_name",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_external_account_credentials",
    srcs = [
        "src/core/lib/security/credentials/external/aws_external_account_credentials.cc",
        "src/core/lib/security/credentials/external/aws_request_signer.cc",
        "src/core/lib/security/credentials/external/external_account_credentials.cc",
        "src/core/lib/security/credentials/external/file_external_account_credentials.cc",
        "src/core/lib/security/credentials/external/url_external_account_credentials.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/external/aws_external_account_credentials.h",
        "src/core/lib/security/credentials/external/aws_request_signer.h",
        "src/core/lib/security/credentials/external/external_account_credentials.h",
        "src/core/lib/security/credentials/external/file_external_account_credentials.h",
        "src/core/lib/security/credentials/external/url_external_account_credentials.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:optional",
        "libcrypto",
    ],
    language = "c++",
    deps = [
        "env",
        "gpr",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_oauth2_credentials",
        "grpc_security_base",
        "httpcli",
        "httpcli_ssl_credentials",
        "json",
        "orphanable",
        "ref_counted_ptr",
        "slice_refcount",
        "time",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "httpcli_ssl_credentials",
    srcs = [
        "src/core/lib/http/httpcli_security_connector.cc",
    ],
    hdrs = [
        "src/core/lib/http/httpcli_ssl_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_security_base",
        "handshaker",
        "iomgr_fwd",
        "promise",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_ssl_credentials",
        "unique_type_name",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_types",
    hdrs = [
        "src/core/tsi/ssl_types.h",
    ],
    external_deps = ["libssl"],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_security_base",
    srcs = [
        "src/core/lib/security/context/security_context.cc",
        "src/core/lib/security/credentials/call_creds_util.cc",
        "src/core/lib/security/credentials/composite/composite_credentials.cc",
        "src/core/lib/security/credentials/credentials.cc",
        "src/core/lib/security/credentials/plugin/plugin_credentials.cc",
        "src/core/lib/security/security_connector/security_connector.cc",
        "src/core/lib/security/transport/client_auth_filter.cc",
        "src/core/lib/security/transport/secure_endpoint.cc",
        "src/core/lib/security/transport/security_handshaker.cc",
        "src/core/lib/security/transport/server_auth_filter.cc",
        "src/core/lib/security/transport/tsi_error.cc",
    ],
    hdrs = [
        "src/core/lib/security/context/security_context.h",
        "src/core/lib/security/credentials/call_creds_util.h",
        "src/core/lib/security/credentials/composite/composite_credentials.h",
        "src/core/lib/security/credentials/credentials.h",
        "src/core/lib/security/credentials/plugin/plugin_credentials.h",
        "src/core/lib/security/security_connector/security_connector.h",
        "src/core/lib/security/transport/auth_filters.h",
        "src/core/lib/security/transport/secure_endpoint.h",
        "src/core/lib/security/transport/security_handshaker.h",
        "src/core/lib/security/transport/tsi_error.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["@grpc:public"],
    deps = [
        "activity",
        "arena",
        "arena_promise",
        "basic_seq",
        "channel_args",
        "channel_fwd",
        "closure",
        "config",
        "context",
        "debug_location",
        "event_engine_memory_allocator",
        "exec_ctx",
        "gpr",
        "gpr_atm",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_trace",
        "handshaker",
        "handshaker_factory",
        "handshaker_registry",
        "iomgr_fwd",
        "memory_quota",
        "poll",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "resource_quota_trace",
        "seq",
        "slice",
        "slice_refcount",
        "try_seq",
        "tsi_base",
        "unique_type_name",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_credentials_util",
    srcs = [
        "src/core/lib/security/credentials/tls/tls_utils.cc",
        "src/core/lib/security/security_connector/load_system_roots_fallback.cc",
        "src/core/lib/security/security_connector/load_system_roots_supported.cc",
        "src/core/lib/security/util/json_util.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/tls/tls_utils.h",
        "src/core/lib/security/security_connector/load_system_roots.h",
        "src/core/lib/security/security_connector/load_system_roots_supported.h",
        "src/core/lib/security/util/json_util.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc_base",
        "grpc_security_base",
        "json",
        "useful",
    ],
)

grpc_cc_library(
    name = "tsi_alts_credentials",
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
        "src/core/tsi/alts/handshaker/alts_handshaker_client.cc",
        "src/core/tsi/alts/handshaker/alts_shared_resource.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.cc",
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
        "src/core/tsi/alts/handshaker/alts_handshaker_client.h",
        "src/core/tsi/alts/handshaker/alts_shared_resource.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h",
        "src/core/tsi/alts/handshaker/alts_tsi_handshaker_private.h",
        "src/core/tsi/alts/handshaker/alts_tsi_utils.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h",
        "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h",
    ],
    external_deps = [
        "libssl",
        "libcrypto",
        "upb_lib",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
    deps = [
        "alts_upb",
        "alts_util",
        "arena",
        "config",
        "error",
        "gpr",
        "grpc_base",
        "pollset_set",
        "tsi_base",
        "useful",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_session_cache",
    srcs = [
        "src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_openssl.cc",
    ],
    hdrs = [
        "src/core/tsi/ssl/session_cache/ssl_session.h",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.h",
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
        "grpc_codegen",
        "ref_counted",
        "slice",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_credentials",
    srcs = [
        "src/core/lib/security/security_connector/ssl_utils.cc",
        "src/core/lib/security/security_connector/ssl_utils_config.cc",
        "src/core/tsi/ssl/key_logging/ssl_key_logging.cc",
        "src/core/tsi/ssl_transport_security.cc",
    ],
    hdrs = [
        "src/core/lib/security/security_connector/ssl_utils.h",
        "src/core/lib/security/security_connector/ssl_utils_config.h",
        "src/core/tsi/ssl/key_logging/ssl_key_logging.h",
        "src/core/tsi/ssl_transport_security.h",
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
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_transport_chttp2_alpn",
        "ref_counted",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_ssl_session_cache",
        "tsi_ssl_types",
        "useful",
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
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    language = "c++",
    deps = [
        "google_type_expr_upb",
        "gpr_platform",
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
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "re2",
    ],
    language = "c++",
    deps = ["gpr"],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_rbac_engine",
    srcs = [
        "src/core/lib/security/authorization/grpc_authorization_engine.cc",
        "src/core/lib/security/authorization/matchers.cc",
        "src/core/lib/security/authorization/rbac_policy.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/grpc_authorization_engine.h",
        "src/core/lib/security/authorization/matchers.h",
        "src/core/lib/security/authorization/rbac_policy.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "gpr",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_matchers",
        "resolved_address",
        "sockaddr_utils",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_authorization_provider",
    srcs = [
        "src/core/lib/security/authorization/grpc_authorization_policy_provider.cc",
        "src/core/lib/security/authorization/rbac_translator.cc",
    ],
    hdrs = [
        "src/core/lib/security/authorization/grpc_authorization_policy_provider.h",
        "src/core/lib/security/authorization/rbac_translator.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    deps = [
        "gpr",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_matchers",
        "grpc_public_hdrs",
        "grpc_rbac_engine",
        "grpc_trace",
        "json",
        "ref_counted_ptr",
        "slice_refcount",
        "useful",
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
    deps = [
        "gpr",
        "grpc++",
        "grpc++_codegen_base",
        "grpc_authorization_provider",
        "grpc_public_hdrs",
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
        "absl/memory",
        "absl/strings",
        "absl/types:optional",
        "absl/types:span",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "envoy_config_rbac_upb",
        "google_type_expr_upb",
        "gpr",
        "grpc_authorization_base",
        "grpc_mock_cel",
    ],
)

grpc_cc_library(
    name = "hpack_constants",
    hdrs = [
        "src/core/ext/transport/chttp2/transport/hpack_constants.h",
    ],
    language = "c++",
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "hpack_encoder_table",
    srcs = [
        "src/core/ext/transport/chttp2/transport/hpack_encoder_table.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h",
    ],
    external_deps = ["absl/container:inlined_vector"],
    language = "c++",
    deps = [
        "gpr",
        "hpack_constants",
    ],
)

grpc_cc_library(
    name = "chttp2_flow_control",
    srcs = [
        "src/core/ext/transport/chttp2/transport/flow_control.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/flow_control.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "bdp_estimator",
        "experiments",
        "gpr",
        "grpc_trace",
        "http2_settings",
        "memory_quota",
        "pid_controller",
        "time",
        "useful",
    ],
)

grpc_cc_library(
    name = "huffsyms",
    srcs = [
        "src/core/ext/transport/chttp2/transport/huffsyms.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/huffsyms.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "decode_huff",
    srcs = [
        "src/core/ext/transport/chttp2/transport/decode_huff.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/decode_huff.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "http2_settings",
    srcs = [
        "src/core/ext/transport/chttp2/transport/http2_settings.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/http2_settings.h",
    ],
    deps = [
        "gpr_platform",
        "http2_errors",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.cc",
        "src/core/ext/transport/chttp2/transport/bin_encoder.cc",
        "src/core/ext/transport/chttp2/transport/chttp2_transport.cc",
        "src/core/ext/transport/chttp2/transport/context_list.cc",
        "src/core/ext/transport/chttp2/transport/frame_data.cc",
        "src/core/ext/transport/chttp2/transport/frame_goaway.cc",
        "src/core/ext/transport/chttp2/transport/frame_ping.cc",
        "src/core/ext/transport/chttp2/transport/frame_rst_stream.cc",
        "src/core/ext/transport/chttp2/transport/frame_settings.cc",
        "src/core/ext/transport/chttp2/transport/frame_window_update.cc",
        "src/core/ext/transport/chttp2/transport/hpack_encoder.cc",
        "src/core/ext/transport/chttp2/transport/hpack_parser.cc",
        "src/core/ext/transport/chttp2/transport/hpack_parser_table.cc",
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
        "src/core/ext/transport/chttp2/transport/frame.h",
        "src/core/ext/transport/chttp2/transport/frame_data.h",
        "src/core/ext/transport/chttp2/transport/frame_goaway.h",
        "src/core/ext/transport/chttp2/transport/frame_ping.h",
        "src/core/ext/transport/chttp2/transport/frame_rst_stream.h",
        "src/core/ext/transport/chttp2/transport/frame_settings.h",
        "src/core/ext/transport/chttp2/transport/frame_window_update.h",
        "src/core/ext/transport/chttp2/transport/hpack_encoder.h",
        "src/core/ext/transport/chttp2/transport/hpack_parser.h",
        "src/core/ext/transport/chttp2/transport/hpack_parser_table.h",
        "src/core/ext/transport/chttp2/transport/internal.h",
        "src/core/ext/transport/chttp2/transport/stream_map.h",
        "src/core/ext/transport/chttp2/transport/varint.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:span",
        "absl/types:variant",
    ],
    language = "c++",
    visibility = ["@grpc:grpclb"],
    deps = [
        "arena",
        "bdp_estimator",
        "bitset",
        "chttp2_flow_control",
        "debug_location",
        "decode_huff",
        "experiments",
        "gpr",
        "gpr_atm",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_trace",
        "hpack_constants",
        "hpack_encoder_table",
        "http2_errors",
        "http2_settings",
        "httpcli",
        "huffsyms",
        "iomgr_fwd",
        "iomgr_timer",
        "memory_quota",
        "no_destruct",
        "poll",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "resource_quota_trace",
        "slice",
        "slice_buffer",
        "slice_refcount",
        "status_helper",
        "time",
        "transport_fwd",
        "useful",
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
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_connector",
    srcs = [
        "src/core/ext/transport/chttp2/client/chttp2_connector.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/client/chttp2_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args_preconditioning",
        "channel_stack_type",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_insecure_credentials",
        "grpc_public_hdrs",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_trace",
        "grpc_transport_chttp2",
        "handshaker",
        "handshaker_registry",
        "iomgr_timer",
        "orphanable",
        "ref_counted_ptr",
        "resolved_address",
        "slice",
        "sockaddr_utils",
        "tcp_connect_handshaker",
        "transport_fwd",
        "unique_type_name",
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
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_insecure_credentials",
        "grpc_security_base",
        "grpc_trace",
        "grpc_transport_chttp2",
        "handshaker",
        "handshaker_registry",
        "iomgr_fwd",
        "iomgr_timer",
        "memory_quota",
        "orphanable",
        "pollset_set",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "slice",
        "sockaddr_utils",
        "time",
        "transport_fwd",
        "unique_type_name",
        "uri_parser",
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
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "channel_args_preconditioning",
        "channel_stack_type",
        "config",
        "debug_location",
        "gpr",
        "grpc_base",
        "grpc_codegen",
        "grpc_public_hdrs",
        "grpc_trace",
        "iomgr_fwd",
        "ref_counted_ptr",
        "slice",
        "slice_buffer",
        "time",
        "transport_fwd",
    ],
)

grpc_cc_library(
    name = "tsi_base",
    srcs = [
        "src/core/tsi/transport_security.cc",
        "src/core/tsi/transport_security_grpc.cc",
    ],
    hdrs = [
        "src/core/tsi/transport_security.h",
        "src/core/tsi/transport_security_grpc.h",
        "src/core/tsi/transport_security_interface.h",
    ],
    language = "c++",
    visibility = ["@grpc:tsi_interface"],
    deps = [
        "gpr",
        "grpc_trace",
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
    external_deps = ["upb_lib"],
    language = "c++",
    visibility = ["@grpc:tsi"],
    deps = [
        "alts_upb",
        "gpr",
        "grpc_trace",
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
        "grpc_base",
        "tsi_alts_credentials",
        "tsi_base",
        "tsi_fake_credentials",
        "tsi_local_credentials",
        "tsi_ssl_credentials",
        "useful",
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
        "arena",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "config",
        "env",
        "error",
        "gpr",
        "gpr_manual_constructor",
        "grpc",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_health_upb",
        "grpc_security_base",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_transport_inproc",
        "grpcpp_call_metric_recorder",
        "iomgr_timer",
        "json",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "slice",
        "slice_buffer",
        "slice_refcount",
        "thread_quota",
        "time",
        "useful",
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
        "absl/memory",
        "upb_lib",
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
        "arena",
        "channel_init",
        "config",
        "gpr",
        "gpr_manual_constructor",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc_base",
        "grpc_codegen",
        "grpc_health_upb",
        "grpc_insecure_credentials",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_transport_inproc",
        "grpc_unsecure",
        "grpcpp_call_metric_recorder",
        "iomgr_timer",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "slice",
        "time",
        "useful",
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
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
    deps = [
        "grpc++_public_hdrs",
        "grpc_codegen",
    ],
)

grpc_cc_library(
    name = "grpc++_codegen_base_src",
    srcs = [
        "src/cpp/codegen/codegen_init.cc",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "grpc++_codegen_base",
        "grpc++_public_hdrs",
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
    tags = ["nofixdeps"],
    visibility = ["@grpc:public"],
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
    srcs = [
        "src/cpp/server/orca/call_metric_recorder.cc",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/call_metric_recorder.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "arena",
        "grpc++_codegen_base",
        "grpc++_public_hdrs",
        "grpc_backend_metric_data",
        "xds_orca_upb",
    ],
)

grpc_cc_library(
    name = "grpcpp_orca_interceptor",
    srcs = [
        "src/cpp/server/orca/orca_interceptor.cc",
    ],
    hdrs = [
        "src/cpp/server/orca/orca_interceptor.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "grpc++",
        "grpc_base",
        "grpcpp_call_metric_recorder",
    ],
)

grpc_cc_library(
    name = "grpcpp_orca_service",
    srcs = [
        "src/cpp/server/orca/orca_service.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
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
        "default_event_engine",
        "gpr",
        "grpc++",
        "grpc++_codegen_base",
        "grpc_base",
        "protobuf_duration_upb",
        "ref_counted",
        "ref_counted_ptr",
        "time",
        "xds_orca_service_upb",
        "xds_orca_upb",
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
        "grpc++_codegen_base",
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
    select_deps = [{
        "grpc_no_xds": [],
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
    name = "grpc++_core_stats",
    srcs = [
        "src/cpp/util/core_stats.cc",
    ],
    hdrs = [
        "src/cpp/util/core_stats.h",
    ],
    language = "c++",
    deps = [
        "gpr",
        "gpr_atm",
        "grpc_base",
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
        "src/cpp/ext/filters/census/open_census_call_tracer.h",
        "src/cpp/ext/filters/census/rpc_encoding.h",
        "src/cpp/ext/filters/census/server_filter.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/time",
        "absl/types:optional",
        "opencensus-trace",
        "opencensus-trace-context_util",
        "opencensus-trace-propagation",
        "opencensus-trace-span_context",
        "opencensus-tags",
        "opencensus-tags-context_util",
        "opencensus-stats",
        "opencensus-context",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:grpc_opencensus_plugin"],
    deps = [
        "arena",
        "census",
        "channel_stack_type",
        "debug_location",
        "gpr",
        "grpc++",
        "grpc++_base",
        "grpc_base",
        "slice",
        "slice_buffer",
        "slice_refcount",
    ],
)

grpc_cc_library(
    name = "json",
    srcs = [
        "src/core/lib/json/json_reader.cc",
        "src/core/lib/json/json_writer.cc",
    ],
    hdrs = [
        "src/core/lib/json/json.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "json_util",
    srcs = ["src/core/lib/json/json_util.cc"],
    hdrs = ["src/core/lib/json/json_util.h"],
    external_deps = ["absl/strings"],
    deps = [
        "error",
        "gpr",
        "json",
        "json_args",
        "json_object_loader",
        "no_destruct",
        "time",
        "validation_errors",
    ],
)

grpc_cc_library(
    name = "json_args",
    hdrs = ["src/core/lib/json/json_args.h"],
    external_deps = ["absl/strings"],
    deps = ["gpr"],
)

grpc_cc_library(
    name = "json_object_loader",
    srcs = ["src/core/lib/json/json_object_loader.cc"],
    hdrs = ["src/core/lib/json/json_object_loader.h"],
    external_deps = [
        "absl/meta:type_traits",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "gpr",
        "json",
        "json_args",
        "no_destruct",
        "ref_counted_ptr",
        "time",
        "validation_errors",
    ],
)

grpc_cc_library(
    name = "json_channel_args",
    hdrs = ["src/core/lib/json/json_channel_args.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "gpr",
        "json_args",
    ],
)

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
