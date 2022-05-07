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

config_setting(
    name = "use_abseil_status",
    values = {"define": "use_abseil_status=true"},
)

python_config_settings()

# This should be updated along with build_handwritten.yaml
g_stands_for = "gridman"  # @unused

core_version = "24.0.0"  # @unused

version = "1.47.0-dev"  # @unused

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
    "include/grpc/grpc_security.h",
    "include/grpc/grpc_security_constants.h",
    "include/grpc/slice.h",
    "include/grpc/slice_buffer.h",
    "include/grpc/status.h",
    "include/grpc/load_reporting.h",
    "include/grpc/support/workaround_list.h",
]

GRPC_PUBLIC_EVENT_ENGINE_HDRS = [
    "include/grpc/event_engine/endpoint_config.h",
    "include/grpc/event_engine/event_engine.h",
    "include/grpc/event_engine/port.h",
    "include/grpc/event_engine/memory_allocator.h",
    "include/grpc/event_engine/memory_request.h",
    "include/grpc/event_engine/internal/memory_allocator_impl.h",
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
]

grpc_cc_library(
    name = "gpr",
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    standalone = True,
    tags = ["avoid_dep"],
    visibility = ["@grpc:public"],
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "atomic_utils",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/atomic_utils.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "grpc_unsecure",
    srcs = [
        "src/core/lib/surface/init.cc",
        "src/core/plugin_registry/grpc_plugin_registry.cc",
        "src/core/plugin_registry/grpc_plugin_registry_noextra.cc",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    standalone = True,
    tags = ["avoid_dep"],
    visibility = ["@grpc:public"],
    deps = [
        "config",
        "gpr_base",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_common",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
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
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    select_deps = [
        {
            "grpc_no_xds": [],
            "//conditions:default": GRPC_XDS_TARGETS,
        },
    ],
    standalone = True,
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "config",
        "gpr_base",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_common",
        "grpc_secure",
        "grpc_security_base",
        "grpc_trace",
        "http_connect_handshaker",
        "slice",
        "tcp_connect_handshaker",
    ],
)

grpc_cc_library(
    name = "grpc++_public_hdrs",
    hdrs = GRPCXX_PUBLIC_HDRS,
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    visibility = ["@grpc:public"],
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
    standalone = True,
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "grpc++_internals",
        "slice",
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
        "src/cpp/common/tls_certificate_verifier.cc",
        "src/cpp/common/tls_credentials_options.cc",
        "src/cpp/server/insecure_server_credentials.cc",
        "src/cpp/server/secure_server_credentials.cc",
    ],
    hdrs = [
        "src/cpp/client/secure_credentials.h",
        "src/cpp/common/secure_auth_context.h",
        "src/cpp/server/secure_server_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/synchronization",
        "absl/container:inlined_vector",
        "absl/strings",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "error",
        "gpr_base",
        "grpc",
        "grpc++_base",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_secure",
        "grpc_security_base",
        "json",
        "ref_counted_ptr",
        "slice",
    ],
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
        "absl/container:flat_hash_map",
        "absl/memory",
        "absl/status",
        "absl/strings",
        "absl/synchronization",
        "absl/status:statusor",
        "absl/time",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/security/binder_security_policy.h",
        "include/grpcpp/create_channel_binder.h",
        "include/grpcpp/security/binder_credentials.h",
    ],
    deps = [
        "config",
        "gpr",
        "gpr_base",
        "gpr_platform",
        "grpc",
        "grpc++_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "iomgr_port",
        "orphanable",
        "slice_refcount",
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
        "absl/container:inlined_vector",
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
    visibility = ["@grpc:xds"],
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
    tags = ["avoid_dep"],
    visibility = ["@grpc:public"],
    deps = [
        "gpr",
        "grpc++_base_unsecure",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_codegen_proto",
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
        "upb_lib",
    ],
    language = "c++",
    standalone = True,
    visibility = ["@grpc:tsi"],
    deps = [
        "alts_upb",
        "alts_util",
        "gpr_base",
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
        "gpr_base",
        "grpc_base",
        "grpc_trace",
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
    deps = [
        "gpr_codegen",
    ],
)

grpc_cc_library(
    name = "useful",
    hdrs = ["src/core/lib/gpr/useful.h"],
    language = "c++",
    deps = [
        "gpr_platform",
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
        "src/core/lib/gpr/tmpfile_msys.cc",
        "src/core/lib/gpr/tmpfile_posix.cc",
        "src/core/lib/gpr/tmpfile_windows.cc",
        "src/core/lib/gpr/wrap_memcpy.cc",
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
        "src/core/lib/gpr/env.h",
        "src/core/lib/gpr/murmur_hash.h",
        "src/core/lib/gpr/spinlock.h",
        "src/core/lib/gpr/string.h",
        "src/core/lib/gpr/string_windows.h",
        "src/core/lib/gpr/time_precise.h",
        "src/core/lib/gpr/tmpfile.h",
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
        "upb_lib",
    ],
    language = "c++",
    public_hdrs = GPR_PUBLIC_HDRS,
    visibility = ["@grpc:alt_gpr_base_legacy"],
    deps = [
        "construct_destruct",
        "debug_location",
        "google_rpc_status_upb",
        "gpr_codegen",
        "gpr_tls",
        "grpc_codegen",
        "protobuf_any_upb",
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
    external_deps = ["absl/utility"],
    deps = [
        "arena",
        # TODO(ctiller): weaken this to just arena when that splits into its own target
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "capture",
    external_deps = ["absl/utility"],
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/capture.h"],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "construct_destruct",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/construct_destruct.h"],
)

grpc_cc_library(
    name = "cpp_impl_of",
    hdrs = ["src/core/lib/gprpp/cpp_impl_of.h"],
    language = "c++",
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
    visibility = ["@grpc:public"],
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
        "channel_args_preconditioning",
        "channel_creds_registry",
        "channel_init",
        "gpr_base",
        "grpc_resolver",
        "handshaker_registry",
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
    external_deps = [
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/match.h"],
    deps = [
        "gpr_platform",
        "overload",
    ],
)

grpc_cc_library(
    name = "table",
    external_deps = ["absl/utility"],
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/table.h"],
    deps = [
        "bitset",
        "gpr_platform",
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
    name = "orphanable",
    language = "c++",
    public_hdrs = ["src/core/lib/gprpp/orphanable.h"],
    visibility = ["@grpc:client_channel"],
    deps = [
        "debug_location",
        "gpr_base",
        "grpc_trace",
        "ref_counted",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "poll",
    external_deps = [
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/poll.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "call_push_pull",
    hdrs = ["src/core/lib/promise/call_push_pull.h"],
    language = "c++",
    deps = [
        "bitset",
        "construct_destruct",
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
    deps = [
        "activity",
        "gpr_platform",
        "grpc_base",
        "poll",
    ],
)

grpc_cc_library(
    name = "promise",
    external_deps = [
        "absl/types:optional",
        "absl/status",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/promise.h",
    ],
    deps = [
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "arena_promise",
    external_deps = [
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/arena_promise.h",
    ],
    deps = [
        "arena",
        "gpr_base",
        "poll",
    ],
)

grpc_cc_library(
    name = "promise_like",
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
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/promise_factory.h",
    ],
    deps = [
        "gpr_platform",
        "poll",
        "promise_like",
    ],
)

grpc_cc_library(
    name = "if",
    external_deps = [
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = ["src/core/lib/promise/if.h"],
    deps = [
        "gpr_platform",
        "poll",
        "promise_factory",
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
        "absl/types:variant",
        "absl/status:statusor",
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
    name = "switch",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/switch.h",
    ],
    deps = ["gpr_platform"],
)

grpc_cc_library(
    name = "basic_join",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/basic_join.h",
    ],
    deps = [
        "bitset",
        "construct_destruct",
        "gpr_platform",
        "poll",
        "promise_factory",
    ],
)

grpc_cc_library(
    name = "join",
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
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/try_join.h",
    ],
    deps = [
        "basic_join",
        "gpr_platform",
        "promise_status",
    ],
)

grpc_cc_library(
    name = "basic_seq",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/detail/basic_seq.h",
    ],
    deps = [
        "construct_destruct",
        "gpr_platform",
        "poll",
        "promise_factory",
        "switch",
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
    ],
)

grpc_cc_library(
    name = "try_seq",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/try_seq.h",
    ],
    deps = [
        "basic_seq",
        "gpr_platform",
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
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/activity.h",
    ],
    deps = [
        "atomic_utils",
        "construct_destruct",
        "context",
        "gpr_base",
        "gpr_codegen",
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
        "exec_ctx",
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "wait_set",
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/wait_set.h",
    ],
    deps = [
        "activity",
        "gpr_platform",
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
    ],
)

grpc_cc_library(
    name = "latch",
    external_deps = [
        "absl/status",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/latch.h",
    ],
    deps = [
        "activity",
        "gpr_platform",
        "intra_activity_waiter",
    ],
)

grpc_cc_library(
    name = "observable",
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/observable.h",
    ],
    deps = [
        "activity",
        "gpr_platform",
        "wait_set",
    ],
)

grpc_cc_library(
    name = "pipe",
    external_deps = [
        "absl/status",
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/promise/pipe.h",
    ],
    deps = [
        "activity",
        "arena",
        "gpr_platform",
        "intra_activity_waiter",
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
    visibility = ["@grpc:ref_counted_ptr"],
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "handshaker",
    srcs = [
        "src/core/lib/transport/handshaker.cc",
    ],
    external_deps = [
        "absl/strings",
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
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_trace",
        "slice",
    ],
)

grpc_cc_library(
    name = "handshaker_factory",
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/handshaker_factory.h",
    ],
    deps = [
        "gpr_base",
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
        "gpr_base",
        "handshaker_factory",
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
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/http_connect_handshaker.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "config",
        "debug_location",
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "handshaker",
        "handshaker_factory",
        "handshaker_registry",
        "httpcli",
        "iomgr_fwd",
        "ref_counted_ptr",
        "uri_parser",
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
    ],
    language = "c++",
    public_hdrs = [
        "src/core/lib/transport/tcp_connect_handshaker.h",
    ],
    deps = [
        "config",
        "debug_location",
        "gpr_base",
        "gpr_platform",
        "grpc_base",
        "grpc_codegen",
        "handshaker",
        "handshaker_factory",
        "handshaker_registry",
        "iomgr_fwd",
        "ref_counted_ptr",
        "resolved_address",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry",
    hdrs = [
        "src/core/lib/security/credentials/channel_creds_registry.h",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "json",
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
    language = "c++",
    deps = [
        "gpr_platform",
        "ref_counted",
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
        "absl/status",
        "absl/strings",
        "absl/utility",
    ],
    deps = [
        "activity",
        "dual_ref_counted",
        "event_engine_memory_allocator",
        "exec_ctx_wakeup_scheduler",
        "gpr_base",
        "grpc_trace",
        "loop",
        "map",
        "orphanable",
        "poll",
        "race",
        "ref_counted_ptr",
        "resource_quota_trace",
        "seq",
        "slice_refcount",
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
        "context",
        "gpr_base",
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
    deps = [
        "gpr_base",
        "ref_counted",
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
    deps = [
        "cpp_impl_of",
        "gpr_base",
        "memory_quota",
        "ref_counted",
        "thread_quota",
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
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "slice",
    srcs = [
        "src/core/lib/slice/slice.cc",
        "src/core/lib/slice/slice_string_helpers.cc",
    ],
    hdrs = [
        "src/core/lib/slice/slice.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
    ],
    deps = [
        "gpr_base",
        "ref_counted",
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
        "src/core/lib/iomgr/error_internal.h",
    ],
    deps = [
        "gpr",
        "grpc_codegen",
        "grpc_trace",
        "slice",
        "slice_refcount",
        "useful",
    ],
)

grpc_cc_library(
    name = "closure",
    hdrs = [
        "src/core/lib/iomgr/closure.h",
    ],
    deps = [
        "error",
        "gpr",
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
    ],
    deps = [
        "gpr",
        "gpr_codegen",
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
        "error",
        "gpr_base",
        "gpr_tls",
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
        "gpr_base",
        "grpc_sockaddr",
        "resolved_address",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "iomgr_port",
    hdrs = [
        "src/core/lib/iomgr/port.h",
    ],
)

grpc_cc_library(
    name = "iomgr_fwd",
    hdrs = [
        "src/core/lib/iomgr/iomgr_fwd.h",
    ],
)

grpc_cc_library(
    name = "grpc_sockaddr",
    srcs = [
        "src/core/lib/event_engine/sockaddr.cc",
        "src/core/lib/iomgr/sockaddr_utils_posix.cc",
        "src/core/lib/iomgr/socket_utils_windows.cc",
    ],
    hdrs = [
        "src/core/lib/event_engine/sockaddr.h",
        "src/core/lib/iomgr/sockaddr.h",
        "src/core/lib/iomgr/sockaddr_posix.h",
        "src/core/lib/iomgr/sockaddr_windows.h",
        "src/core/lib/iomgr/socket_utils.h",
    ],
    deps = [
        "gpr_base",
        "iomgr_port",
    ],
)

grpc_cc_library(
    name = "avl",
    hdrs = [
        "src/core/lib/avl/avl.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
    ],
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_base_hdrs",
    hdrs = GRPC_PUBLIC_EVENT_ENGINE_HDRS + GRPC_PUBLIC_HDRS,
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/time",
    ],
    deps = [
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "default_event_engine_factory_hdrs",
    hdrs = [
        "src/core/lib/event_engine/event_engine_factory.h",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "default_event_engine_factory",
    srcs = [
        "src/core/lib/event_engine/default_event_engine_factory.cc",
    ],
    external_deps = [
        # TODO(hork): uv, in a subsequent PR
    ],
    deps = [
        "default_event_engine_factory_hdrs",
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "event_engine_common",
    srcs = [
        "src/core/lib/event_engine/resolved_address.cc",
    ],
    deps = [
        "event_engine_base_hdrs",
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "event_engine_base",
    srcs = [
        "src/core/lib/event_engine/event_engine.cc",
    ],
    deps = [
        "default_event_engine_factory",
        "default_event_engine_factory_hdrs",
        "event_engine_base_hdrs",
        "gpr_base",
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
    deps = [
        "gpr_base",
    ],
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
        "gpr_base",
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
    name = "grpc_base",
    srcs = [
        "src/core/lib/address_utils/parse_address.cc",
        "src/core/lib/backoff/backoff.cc",
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
        "src/core/lib/event_engine/sockaddr.cc",
        "src/core/lib/iomgr/buffer_list.cc",
        "src/core/lib/iomgr/call_combiner.cc",
        "src/core/lib/iomgr/cfstream_handle.cc",
        "src/core/lib/iomgr/dualstack_socket_posix.cc",
        "src/core/lib/iomgr/endpoint.cc",
        "src/core/lib/iomgr/endpoint_cfstream.cc",
        "src/core/lib/iomgr/endpoint_pair_event_engine.cc",
        "src/core/lib/iomgr/endpoint_pair_posix.cc",
        "src/core/lib/iomgr/endpoint_pair_windows.cc",
        "src/core/lib/iomgr/error_cfstream.cc",
        "src/core/lib/iomgr/ev_apple.cc",
        "src/core/lib/iomgr/ev_epoll1_linux.cc",
        "src/core/lib/iomgr/ev_poll_posix.cc",
        "src/core/lib/iomgr/ev_posix.cc",
        "src/core/lib/iomgr/ev_windows.cc",
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
        "src/core/lib/iomgr/iomgr_posix.cc",
        "src/core/lib/iomgr/iomgr_posix_cfstream.cc",
        "src/core/lib/iomgr/iomgr_windows.cc",
        "src/core/lib/iomgr/load_file.cc",
        "src/core/lib/iomgr/lockfree_event.cc",
        "src/core/lib/iomgr/polling_entity.cc",
        "src/core/lib/iomgr/pollset.cc",
        "src/core/lib/iomgr/pollset_set.cc",
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
        "src/core/lib/iomgr/time_averaged_stats.cc",
        "src/core/lib/iomgr/timer.cc",
        "src/core/lib/iomgr/timer_generic.cc",
        "src/core/lib/iomgr/timer_heap.cc",
        "src/core/lib/iomgr/timer_manager.cc",
        "src/core/lib/iomgr/unix_sockets_posix.cc",
        "src/core/lib/iomgr/unix_sockets_posix_noop.cc",
        "src/core/lib/iomgr/wakeup_fd_eventfd.cc",
        "src/core/lib/iomgr/wakeup_fd_nospecial.cc",
        "src/core/lib/iomgr/wakeup_fd_pipe.cc",
        "src/core/lib/iomgr/wakeup_fd_posix.cc",
        "src/core/lib/iomgr/work_serializer.cc",
        "src/core/lib/resource_quota/api.cc",
        "src/core/lib/slice/b64.cc",
        "src/core/lib/slice/percent_encoding.cc",
        "src/core/lib/slice/slice_api.cc",
        "src/core/lib/slice/slice_buffer.cc",
        "src/core/lib/slice/slice_split.cc",
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
        "src/core/lib/transport/bdp_estimator.cc",
        "src/core/lib/transport/byte_stream.cc",
        "src/core/lib/transport/connectivity_state.cc",
        "src/core/lib/transport/error_utils.cc",
        "src/core/lib/transport/parsed_metadata.cc",
        "src/core/lib/transport/status_conversion.cc",
        "src/core/lib/transport/timeout_encoding.cc",
        "src/core/lib/transport/transport.cc",
        "src/core/lib/transport/metadata_batch.cc",
        "src/core/lib/transport/transport_op_string.cc",
    ] +
    # TODO(hork): delete the iomgr glue code when EventEngine is fully
    # integrated, or when it becomes obvious the glue code is unnecessary.
    [
        "src/core/lib/iomgr/event_engine/closure.cc",
        "src/core/lib/iomgr/event_engine/endpoint.cc",
        "src/core/lib/iomgr/event_engine/iomgr.cc",
        "src/core/lib/iomgr/event_engine/pollset.cc",
        "src/core/lib/iomgr/event_engine/resolved_address_internal.cc",
        "src/core/lib/iomgr/event_engine/resolver.cc",
        "src/core/lib/iomgr/event_engine/tcp.cc",
        "src/core/lib/iomgr/event_engine/timer.cc",
    ],
    hdrs = [
        "src/core/lib/transport/error_utils.h",
        "src/core/lib/transport/http2_errors.h",
        "src/core/lib/address_utils/parse_address.h",
        "src/core/lib/backoff/backoff.h",
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
        "src/core/lib/event_engine/sockaddr.h",
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
        "src/core/lib/iomgr/executor/mpmcqueue.h",
        "src/core/lib/iomgr/executor/threadpool.h",
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
        "src/core/lib/iomgr/pollset_set.h",
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
        "src/core/lib/iomgr/time_averaged_stats.h",
        "src/core/lib/iomgr/timer.h",
        "src/core/lib/iomgr/timer_generic.h",
        "src/core/lib/iomgr/timer_heap.h",
        "src/core/lib/iomgr/timer_manager.h",
        "src/core/lib/iomgr/unix_sockets_posix.h",
        "src/core/lib/iomgr/wakeup_fd_pipe.h",
        "src/core/lib/iomgr/wakeup_fd_posix.h",
        "src/core/lib/iomgr/work_serializer.h",
        "src/core/lib/slice/b64.h",
        "src/core/lib/slice/percent_encoding.h",
        "src/core/lib/slice/slice_split.h",
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
        "src/core/lib/transport/bdp_estimator.h",
        "src/core/lib/transport/byte_stream.h",
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
        "src/core/lib/iomgr/error_internal.h",
        "src/core/lib/slice/slice_internal.h",
        "src/core/lib/slice/slice_string_helpers.h",
        "src/core/lib/iomgr/exec_ctx.h",
        "src/core/lib/iomgr/executor.h",
        "src/core/lib/iomgr/combiner.h",
        "src/core/lib/iomgr/iomgr_internal.h",
        "src/core/lib/channel/channel_args.h",
        "src/core/lib/channel/channel_stack_builder.h",
    ] +
    # TODO(hork): delete the iomgr glue code when EventEngine is fully
    # integrated, or when it becomes obvious the glue code is unnecessary.
    [
        "src/core/lib/iomgr/event_engine/closure.h",
        "src/core/lib/iomgr/event_engine/endpoint.h",
        "src/core/lib/iomgr/event_engine/pollset.h",
        "src/core/lib/iomgr/event_engine/promise.h",
        "src/core/lib/iomgr/event_engine/resolved_address_internal.h",
        "src/core/lib/iomgr/event_engine/resolver.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/container:inlined_vector",
        "absl/functional:bind_front",
        "absl/memory",
        "absl/status:statusor",
        "absl/status",
        "absl/strings:str_format",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
        "absl/utility",
        "madler_zlib",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS + GRPC_PUBLIC_EVENT_ENGINE_HDRS,
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "arena",
        "arena_promise",
        "avl",
        "bitset",
        "channel_args",
        "channel_args_preconditioning",
        "channel_stack_builder",
        "channel_stack_type",
        "chunked_vector",
        "closure",
        "config",
        "debug_location",
        "default_event_engine_factory",
        "dual_ref_counted",
        "error",
        "event_engine_base",
        "event_engine_common",
        "exec_ctx",
        "gpr_base",
        "gpr_codegen",
        "gpr_tls",
        "grpc_codegen",
        "grpc_sockaddr",
        "grpc_trace",
        "handshaker_registry",
        "iomgr_port",
        "json",
        "latch",
        "memory_quota",
        "orphanable",
        "poll",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "resource_quota_trace",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "table",
        "thread_quota",
        "time",
        "uri_parser",
        "useful",
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
    deps = [
        "gpr_base",
    ],
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
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "single_set_ptr",
    hdrs = [
        "src/core/lib/gprpp/single_set_ptr.h",
    ],
    language = "c++",
    deps = [
        "gpr_base",
    ],
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
    ],
    language = "c++",
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "channel_stack_type",
        "closure",
        "error",
        "gpr_base",
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
    deps = [
        "grpc_base",
        # standard plugins
        "census",
        "grpc_deadline_filter",
        "grpc_client_authority_filter",
        "grpc_lb_policy_grpclb",
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
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "error",
        "gpr_base",
        "json",
        "service_config_parser",
        "slice",
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
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "config",
        "error",
        "gpr_base",
        "grpc_service_config",
        "json",
        "service_config_parser",
        "slice",
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
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "error",
        "gpr_base",
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
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "gpr_base",
        "grpc_service_config",
        "iomgr_fwd",
        "orphanable",
        "server_address",
        "uri_parser",
    ],
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
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:variant",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "avl",
        "channel_stack_type",
        "dual_ref_counted",
        "gpr_base",
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
        "src/core/ext/filters/client_channel/lb_policy.cc",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.cc",
        "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.cc",
        "src/core/ext/filters/client_channel/lb_policy_registry.cc",
        "src/core/ext/filters/client_channel/local_subchannel_pool.cc",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.cc",
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
        "src/core/ext/filters/client_channel/lb_policy.h",
        "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h",
        "src/core/ext/filters/client_channel/lb_policy/oob_backend_metric.h",
        "src/core/ext/filters/client_channel/lb_policy_factory.h",
        "src/core/ext/filters/client_channel/lb_policy_registry.h",
        "src/core/ext/filters/client_channel/local_subchannel_pool.h",
        "src/core/ext/filters/client_channel/proxy_mapper.h",
        "src/core/ext/filters/client_channel/proxy_mapper_registry.h",
        "src/core/ext/filters/client_channel/resolver_result_parsing.h",
        "src/core/ext/filters/client_channel/retry_filter.h",
        "src/core/ext/filters/client_channel/retry_service_config.h",
        "src/core/ext/filters/client_channel/retry_throttle.h",
        "src/core/ext/filters/client_channel/subchannel.h",
        "src/core/ext/filters/client_channel/subchannel_interface.h",
        "src/core/ext/filters/client_channel/subchannel_interface_internal.h",
        "src/core/ext/filters/client_channel/subchannel_pool_interface.h",
        "src/core/ext/filters/client_channel/subchannel_stream_client.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/status:statusor",
        "upb_lib",
    ],
    language = "c++",
    visibility = ["@grpc:client_channel"],
    deps = [
        "config",
        "debug_location",
        "error",
        "gpr_base",
        "grpc_base",
        "grpc_client_authority_filter",
        "grpc_deadline_filter",
        "grpc_health_upb",
        "grpc_resolver",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "handshaker_registry",
        "http_connect_handshaker",
        "httpcli",
        "json",
        "json_util",
        "orphanable",
        "protobuf_duration_upb",
        "ref_counted",
        "ref_counted_ptr",
        "server_address",
        "slice",
        "sockaddr_utils",
        "time",
        "uri_parser",
        "useful",
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
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_service_config",
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
    language = "c++",
    deps = [
        "arena",
        "gpr_base",
        "grpc_base",
        "grpc_server_config_selector",
        "grpc_service_config",
        "promise",
    ],
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
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_channel_idle_filter",
    srcs = [
        "src/core/ext/filters/channel_idle/channel_idle_filter.cc",
    ],
    hdrs = [
        "src/core/ext/filters/channel_idle/channel_idle_filter.h",
    ],
    deps = [
        "capture",
        "config",
        "exec_ctx_wakeup_scheduler",
        "gpr_base",
        "grpc_base",
        "idle_filter_state",
        "loop",
        "promise",
        "single_set_ptr",
        "sleep",
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
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "slice",
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
        "channel_stack_type",
        "config",
        "gpr_base",
        "grpc_base",
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
    external_deps = ["absl/strings:str_format"],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_service_config",
        "ref_counted",
        "ref_counted_ptr",
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
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "capture",
        "gpr_base",
        "grpc_base",
        "grpc_service_config",
        "json_util",
        "sleep",
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
    external_deps = ["absl/strings:str_format"],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_rbac_engine",
        "grpc_service_config",
        "json_util",
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
        "absl/strings:str_format",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = ["@grpc:http"],
    deps = [
        "call_push_pull",
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_message_size_filter",
        "promise",
        "seq",
        "slice",
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
    visibility = ["@grpc:grpclb"],
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
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
        "absl/memory",
        "absl/container:inlined_vector",
        "absl/strings",
        "absl/strings:str_format",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "config",
        "error",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_grpclb_balancer_addresses",
        "grpc_lb_upb",
        "grpc_resolver_fake",
        "grpc_security_base",
        "grpc_sockaddr",
        "grpc_transport_chttp2_client_connector",
        "orphanable",
        "protobuf_duration_upb",
        "protobuf_timestamp_upb",
        "ref_counted_ptr",
        "server_address",
        "slice",
        "sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_rls",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/rls/rls.cc",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/hash",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "config",
        "dual_ref_counted",
        "gpr_base",
        "gpr_codegen",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_fake_credentials",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_service_config_impl",
        "json",
        "json_util",
        "orphanable",
        "ref_counted",
        "rls_upb",
        "uri_parser",
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
        "src/core/ext/xds/xds_cluster.cc",
        "src/core/ext/xds/xds_cluster_specifier_plugin.cc",
        "src/core/ext/xds/xds_common_types.cc",
        "src/core/ext/xds/xds_endpoint.cc",
        "src/core/ext/xds/xds_http_fault_filter.cc",
        "src/core/ext/xds/xds_http_filters.cc",
        "src/core/ext/xds/xds_http_rbac_filter.cc",
        "src/core/ext/xds/xds_listener.cc",
        "src/core/ext/xds/xds_resource_type.cc",
        "src/core/ext/xds/xds_route_config.cc",
        "src/core/ext/xds/xds_routing.cc",
        "src/core/lib/security/credentials/xds/xds_credentials.cc",
    ],
    hdrs = [
        "src/core/ext/xds/certificate_provider_factory.h",
        "src/core/ext/xds/certificate_provider_registry.h",
        "src/core/ext/xds/certificate_provider_store.h",
        "src/core/ext/xds/file_watcher_certificate_provider_factory.h",
        "src/core/ext/xds/upb_utils.h",
        "src/core/ext/xds/xds_api.h",
        "src/core/ext/xds/xds_bootstrap.h",
        "src/core/ext/xds/xds_certificate_provider.h",
        "src/core/ext/xds/xds_channel_args.h",
        "src/core/ext/xds/xds_client.h",
        "src/core/ext/xds/xds_client_stats.h",
        "src/core/ext/xds/xds_cluster.h",
        "src/core/ext/xds/xds_cluster_specifier_plugin.h",
        "src/core/ext/xds/xds_common_types.h",
        "src/core/ext/xds/xds_endpoint.h",
        "src/core/ext/xds/xds_http_fault_filter.h",
        "src/core/ext/xds/xds_http_filters.h",
        "src/core/ext/xds/xds_http_rbac_filter.h",
        "src/core/ext/xds/xds_listener.h",
        "src/core/ext/xds/xds_resource_type.h",
        "src/core/ext/xds/xds_resource_type_impl.h",
        "src/core/ext/xds/xds_route_config.h",
        "src/core/ext/xds/xds_routing.h",
        "src/core/lib/security/credentials/xds/xds_credentials.h",
    ],
    external_deps = [
        "absl/functional:bind_front",
        "absl/memory",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/container:inlined_vector",
        "upb_lib",
        "upb_textformat_lib",
        "upb_json_lib",
        "re2",
        "upb_reflection",
    ],
    language = "c++",
    deps = [
        "channel_creds_registry",
        "config",
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
        "gpr_base",
        "gpr_codegen",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_fake_credentials",
        "grpc_fault_injection_filter",
        "grpc_lb_xds_channel_args",
        "grpc_matchers",
        "grpc_rbac_filter",
        "grpc_secure",
        "grpc_security_base",
        "grpc_sockaddr",
        "grpc_tls_credentials",
        "grpc_transport_chttp2_client_connector",
        "json",
        "json_util",
        "orphanable",
        "protobuf_any_upb",
        "protobuf_duration_upb",
        "protobuf_struct_upb",
        "protobuf_timestamp_upb",
        "protobuf_wrappers_upb",
        "ref_counted_ptr",
        "rls_config_upb",
        "rls_config_upbdefs",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "uri_parser",
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
    language = "c++",
    deps = [
        "channel_init",
        "config",
        "gpr_base",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_xds_server_config_fetcher",
    srcs = [
        "src/core/ext/xds/xds_server_config_fetcher.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_server_config_selector",
        "grpc_server_config_selector_filter",
        "grpc_service_config_impl",
        "grpc_sockaddr",
        "grpc_xds_channel_stack_modifier",
        "grpc_xds_client",
        "slice_refcount",
        "sockaddr_utils",
        "uri_parser",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry_init",
    srcs = [
        "src/core/lib/security/credentials/channel_creds_registry_init.cc",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_fake_credentials",
        "grpc_secure",
        "grpc_security_base",
        "json",
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
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "error",
        "gpr_base",
        "grpc_base",
        "grpc_xds_client",
        "json_util",
        "slice",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_cds",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/cds.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_xds_client",
        "orphanable",
        "ref_counted_ptr",
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
        "gpr_base",
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
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
        "grpc_lb_policy_ring_hash",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_resolver",
        "grpc_resolver_fake",
        "grpc_xds_client",
        "orphanable",
        "ref_counted_ptr",
        "server_address",
        "uri_parser",
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
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_xds_channel_args",
        "grpc_lb_xds_common",
        "grpc_xds_client",
        "orphanable",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_manager",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/xds/xds_cluster_manager.cc",
    ],
    external_deps = [
        "absl/strings",
        "absl/status",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver_xds_header",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
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
        "gpr_base",
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
        "gpr_base",
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
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_subchannel_list",
        "server_address",
        "sockaddr_utils",
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
        "absl/strings",
        "xxhash",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_subchannel_list",
        "grpc_trace",
        "ref_counted_ptr",
        "sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/round_robin/round_robin.cc",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_subchannel_list",
        "grpc_trace",
        "ref_counted_ptr",
        "server_address",
        "sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_priority",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/priority/priority.cc",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
        "orphanable",
        "ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_target",
    srcs = [
        "src/core/ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_address_filtering",
        "orphanable",
        "ref_counted_ptr",
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
        "absl/strings",
        "absl/strings:str_format",
        "opencensus-stats",
    ],
    language = "c++",
    deps = [
        "config",
        "error",
        "gpr",
        "grpc++_base",
        "grpc_base",
        "grpc_lb_policy_grpclb",
        "grpc_security_base",
        "grpc_sockaddr",
        "promise",
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
        "gpr_codegen",
        "grpc++",
        "grpc_base",
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
        "gpr",
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
    deps = [
        "gpr",
        "gpr_codegen",
        "lb_server_load_reporting_filter",
        "lb_server_load_reporting_service_server_builder_plugin",
        "slice",
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
    external_deps = ["absl/memory"],
    language = "c++",
    deps = [
        "gpr",
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
        "gpr_base",
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
        "opencensus-tags",
    ],
    language = "c++",
    deps = [
        "gpr",
        "gpr_codegen",
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
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_resolver",
        "orphanable",
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
        "gpr_base",
        "grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_native",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/native/dns_resolver.cc",
    ],
    external_deps = [
        "absl/strings",
        "absl/functional:bind_front",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver",
        "grpc_resolver_dns_selection",
        "grpc_trace",
        "polling_resolver",
        "server_address",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_ares",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/dns_resolver_ares.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_event_engine.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_event_engine.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc",
    ],
    hdrs = [
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h",
        "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
        "absl/container:inlined_vector",
        "address_sorting",
        "cares",
    ],
    language = "c++",
    deps = [
        "config",
        "error",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_grpclb_balancer_addresses",
        "grpc_resolver",
        "grpc_resolver_dns_selection",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_sockaddr",
        "iomgr_port",
        "json",
        "polling_resolver",
        "server_address",
        "sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver",
        "server_address",
        "slice",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_binder",
    srcs = [
        "src/core/ext/filters/client_channel/resolver/binder/binder_resolver.cc",
    ],
    external_deps = [
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver",
        "iomgr_port",
        "server_address",
        "slice",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_fake",
    srcs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.cc"],
    hdrs = ["src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"],
    language = "c++",
    visibility = [
        "//test:__subpackages__",
        "@grpc:grpc_resolver_fake",
    ],
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver",
        "server_address",
        "slice",
        "useful",
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
        "re2",
        "absl/random",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_lb_policy_ring_hash",
        "grpc_resolver",
        "grpc_service_config_impl",
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
        "alts_util",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_resolver",
        "grpc_xds_client",
        "httpcli",
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
        "absl/functional:bind_front",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    visibility = ["@grpc:httpcli"],
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
        "ref_counted_ptr",
        "sockaddr_utils",
        "tcp_connect_handshaker",
        "useful",
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
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_trace",
        "promise",
        "slice_refcount",
        "sockaddr_utils",
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
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    visibility = [
        "@grpc:public",
    ],
    deps = [
        "gpr_base",
        "grpc_base",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
        "handshaker",
        "promise",
        "ref_counted_ptr",
        "tsi_fake_credentials",
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
    language = "c++",
    deps = [
        "gpr",
        "grpc_security_base",
        "promise",
        "ref_counted_ptr",
        "tsi_local_credentials",
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
        "gpr",
        "grpc_base",
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
        "absl/strings:str_format",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_security_base",
        "grpc_sockaddr",
        "promise",
        "ref_counted_ptr",
        "sockaddr_utils",
        "tsi_local_credentials",
        "uri_parser",
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
        "libssl",
        "upb_lib",
        "upb_lib_descriptor",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "alts_util",
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
        "promise",
        "ref_counted_ptr",
        "tsi_alts_credentials",
        "tsi_base",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_transport_chttp2_alpn",
        "handshaker",
        "promise",
        "ref_counted_ptr",
        "tsi_base",
        "tsi_ssl_credentials",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "alts_util",
        "gpr_base",
        "grpc_alts_credentials",
        "grpc_base",
        "grpc_codegen",
        "grpc_external_account_credentials",
        "grpc_jwt_credentials",
        "grpc_lb_xds_channel_args",
        "grpc_oauth2_credentials",
        "grpc_security_base",
        "grpc_ssl_credentials",
        "httpcli",
        "httpcli_ssl_credentials",
        "ref_counted_ptr",
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
        "absl/functional:bind_front",
        "absl/strings",
        "libssl",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "promise",
        "tsi_base",
        "tsi_ssl_credentials",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
        "promise",
        "ref_counted_ptr",
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
        "absl/strings",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "httpcli",
        "httpcli_ssl_credentials",
        "json",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "tsi_ssl_types",
        "uri_parser",
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
        "absl/container:inlined_vector",
        "absl/strings",
        "absl/strings:str_format",
        "absl/status",
    ],
    language = "c++",
    deps = [
        "capture",
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_security_base",
        "httpcli",
        "httpcli_ssl_credentials",
        "json",
        "promise",
        "ref_counted_ptr",
        "uri_parser",
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
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_oauth2_credentials",
        "grpc_security_base",
        "httpcli",
        "httpcli_ssl_credentials",
        "slice_refcount",
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
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
        "promise",
        "ref_counted_ptr",
        "tsi_ssl_credentials",
    ],
)

grpc_cc_library(
    name = "grpc_secure",
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["@grpc:public"],
    deps = [
        "config",
        "gpr_base",
        "grpc_alts_credentials",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_credentials_util",
        "grpc_external_account_credentials",
        "grpc_fake_credentials",
        "grpc_google_default_credentials",
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
        "httpcli",
        "httpcli_ssl_credentials",
        "json",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "slice",
        "slice_refcount",
        "sockaddr_utils",
        "tsi_base",
        "uri_parser",
        "useful",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_types",
    hdrs = [
        "src/core/tsi/ssl_types.h",
    ],
    external_deps = [
        "libssl",
    ],
    language = "c++",
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
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    visibility = ["@grpc:public"],
    deps = [
        "arena",
        "arena_promise",
        "capture",
        "config",
        "gpr_base",
        "grpc_base",
        "grpc_trace",
        "handshaker",
        "json",
        "memory_quota",
        "promise",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "resource_quota_trace",
        "try_seq",
        "tsi_base",
    ],
)

grpc_cc_library(
    name = "grpc_credentials_util",
    srcs = [
        "src/core/lib/security/credentials/tls/tls_utils.cc",
        "src/core/lib/security/security_connector/load_system_roots_fallback.cc",
        "src/core/lib/security/security_connector/load_system_roots_linux.cc",
        "src/core/lib/security/util/json_util.cc",
    ],
    hdrs = [
        "src/core/lib/security/credentials/tls/tls_utils.h",
        "src/core/lib/security/security_connector/load_system_roots.h",
        "src/core/lib/security/security_connector/load_system_roots_linux.h",
        "src/core/lib/security/util/json_util.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/strings",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_security_base",
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
    visibility = ["@grpc:public"],
    deps = [
        "alts_util",
        "arena",
        "config",
        "error",
        "gpr_base",
        "grpc_base",
        "tsi_base",
        "useful",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_credentials",
    srcs = [
        "src/core/lib/security/security_connector/ssl_utils.cc",
        "src/core/lib/security/security_connector/ssl_utils_config.cc",
        "src/core/tsi/ssl/key_logging/ssl_key_logging.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.cc",
        "src/core/tsi/ssl/session_cache/ssl_session_openssl.cc",
        "src/core/tsi/ssl_transport_security.cc",
    ],
    hdrs = [
        "src/core/lib/security/security_connector/ssl_utils.h",
        "src/core/lib/security/security_connector/ssl_utils_config.h",
        "src/core/tsi/ssl/key_logging/ssl_key_logging.h",
        "src/core/tsi/ssl/session_cache/ssl_session.h",
        "src/core/tsi/ssl/session_cache/ssl_session_cache.h",
        "src/core/tsi/ssl_transport_security.h",
    ],
    external_deps = [
        "absl/memory",
        "absl/strings",
        "libssl",
        "libcrypto",
    ],
    language = "c++",
    visibility = ["@grpc:public"],
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_credentials_util",
        "grpc_security_base",
        "grpc_transport_chttp2_alpn",
        "ref_counted_ptr",
        "tsi_base",
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
    language = "c++",
    deps = [
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
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_base",
    ],
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "gpr_base",
        "grpc_authorization_base",
        "grpc_base",
        "grpc_matchers",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    public_hdrs = GRPC_PUBLIC_HDRS,
    deps = [
        "gpr_base",
        "grpc_base",
        "grpc_matchers",
        "grpc_rbac_engine",
        "useful",
    ],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc++_authorization_provider",
    srcs = [
        "src/cpp/server/authorization_policy_provider.cc",
    ],
    external_deps = [
        "absl/synchronization",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    deps = [
        "gpr_base",
        "grpc++_codegen_base",
        "grpc_authorization_provider",
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
    ],
    language = "c++",
    deps = [
        "envoy_config_rbac_upb",
        "gpr_base",
        "grpc_base",
        "grpc_mock_cel",
        "grpc_rbac_engine",
        "sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "hpack_constants",
    hdrs = [
        "src/core/ext/transport/chttp2/transport/hpack_constants.h",
    ],
    language = "c++",
    deps = [
        "gpr_platform",
    ],
)

grpc_cc_library(
    name = "hpack_encoder_table",
    srcs = [
        "src/core/ext/transport/chttp2/transport/hpack_encoder_table.cc",
    ],
    hdrs = [
        "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
    ],
    language = "c++",
    deps = [
        "gpr",
        "hpack_constants",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2",
    srcs = [
        "src/core/ext/transport/chttp2/transport/bin_decoder.cc",
        "src/core/ext/transport/chttp2/transport/bin_encoder.cc",
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
        "src/core/ext/transport/chttp2/transport/hpack_parser_table.cc",
        "src/core/ext/transport/chttp2/transport/http2_settings.cc",
        "src/core/ext/transport/chttp2/transport/huffsyms.cc",
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
        "src/core/ext/transport/chttp2/transport/hpack_parser_table.h",
        "src/core/ext/transport/chttp2/transport/http2_settings.h",
        "src/core/ext/transport/chttp2/transport/huffsyms.h",
        "src/core/ext/transport/chttp2/transport/internal.h",
        "src/core/ext/transport/chttp2/transport/stream_map.h",
        "src/core/ext/transport/chttp2/transport/varint.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/memory",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:span",
        "absl/types:variant",
        "absl/utility",
    ],
    language = "c++",
    visibility = ["@grpc:grpclb"],
    deps = [
        "arena",
        "bitset",
        "chunked_vector",
        "debug_location",
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_http_filters",
        "grpc_resolver",
        "grpc_trace",
        "grpc_transport_chttp2_alpn",
        "hpack_constants",
        "hpack_encoder_table",
        "httpcli",
        "iomgr_fwd",
        "memory_quota",
        "orphanable",
        "pid_controller",
        "ref_counted",
        "ref_counted_ptr",
        "resource_quota",
        "resource_quota_trace",
        "slice",
        "slice_refcount",
        "time",
        "uri_parser",
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
        "gpr_base",
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
        "absl/container:inlined_vector",
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    deps = [
        "channel_args_preconditioning",
        "channel_stack_type",
        "config",
        "debug_location",
        "gpr_base",
        "grpc_base",
        "grpc_client_channel",
        "grpc_codegen",
        "grpc_insecure_credentials",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_trace",
        "grpc_transport_chttp2",
        "handshaker",
        "handshaker_registry",
        "orphanable",
        "resolved_address",
        "slice",
        "sockaddr_utils",
        "tcp_connect_handshaker",
        "uri_parser",
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
    ],
    language = "c++",
    deps = [
        "config",
        "debug_location",
        "gpr_base",
        "grpc_base",
        "grpc_codegen",
        "grpc_http_filters",
        "grpc_insecure_credentials",
        "grpc_resolver",
        "grpc_security_base",
        "grpc_trace",
        "grpc_transport_chttp2",
        "handshaker",
        "handshaker_registry",
        "iomgr_fwd",
        "memory_quota",
        "orphanable",
        "ref_counted",
        "ref_counted_ptr",
        "resolved_address",
        "resource_quota",
        "slice",
        "sockaddr_utils",
        "time",
        "uri_parser",
        "useful",
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
        "gpr_base",
        "grpc_base",
        "grpc_trace",
        "slice",
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
    external_deps = [
        "upb_lib",
    ],
    language = "c++",
    visibility = ["@grpc:tsi"],
    deps = [
        "alts_upb",
        "gpr",
        "grpc_base",
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
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    external_deps = [
        "absl/synchronization",
        "absl/memory",
        "upb_lib",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    visibility = ["@grpc:alt_grpc++_base_legacy"],
    deps = [
        "config",
        "gpr_base",
        "grpc",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_internal_hdrs_only",
        "grpc_base",
        "grpc_codegen",
        "grpc_health_upb",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_transport_inproc",
        "ref_counted",
        "useful",
    ],
)

grpc_cc_library(
    name = "grpc++_base_unsecure",
    srcs = GRPCXX_SRCS,
    hdrs = GRPCXX_HDRS,
    external_deps = [
        "absl/synchronization",
        "absl/memory",
        "upb_lib",
        "protobuf_headers",
    ],
    language = "c++",
    public_hdrs = GRPCXX_PUBLIC_HDRS,
    tags = ["avoid_dep"],
    visibility = ["@grpc:alt_grpc++_base_unsecure_legacy"],
    deps = [
        "config",
        "gpr_base",
        "grpc++_codegen_base",
        "grpc++_codegen_base_src",
        "grpc++_internal_hdrs_only",
        "grpc_base",
        "grpc_codegen",
        "grpc_health_upb",
        "grpc_insecure_credentials",
        "grpc_service_config",
        "grpc_service_config_impl",
        "grpc_trace",
        "grpc_transport_inproc",
        "grpc_unsecure",
        "ref_counted",
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
    visibility = ["@grpc:public"],
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
    language = "c++",
    public_hdrs = [
        "include/grpc++/ext/proto_server_reflection_plugin.h",
        "include/grpcpp/ext/proto_server_reflection_plugin.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpcpp_orca",
    srcs = [
        "src/cpp/server/orca/orca_service.cc",
    ],
    external_deps = [
        "upb_lib",
    ],
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/orca_service.h",
    ],
    visibility = ["@grpc:public"],
    deps = [
        "grpc++",
        "grpc++_codegen_base",
        "grpc_base",
        "protobuf_duration_upb",
        "ref_counted",
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
    language = "c++",
    public_hdrs = [
        "include/grpcpp/ext/channelz_service_plugin.h",
    ],
    visibility = ["@grpc:channelz"],
    deps = [
        "gpr",
        "grpc",
        "grpc++",
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
    external_deps = ["absl/status:statusor"],
    language = "c++",
    deps = [
        "gpr",
        "grpc",
        "grpc++_codegen_base",
        "grpc++_internals",
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
    external_deps = [
        "gtest",
    ],
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
        "gpr_base",
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
        "grpc++",
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
        "absl-base",
        "absl-time",
        "absl/strings",
        "opencensus-trace",
        "opencensus-trace-context_util",
        "opencensus-trace-propagation",
        "opencensus-tags",
        "opencensus-tags-context_util",
        "opencensus-stats",
        "opencensus-context",
    ],
    language = "c++",
    visibility = ["@grpc:grpc_opencensus_plugin"],
    deps = [
        "census",
        "gpr_base",
        "grpc++",
        "grpc_base",
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
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "exec_ctx",
        "gpr_base",
    ],
)

grpc_cc_library(
    name = "json_util",
    srcs = ["src/core/lib/json/json_util.cc"],
    hdrs = ["src/core/lib/json/json_util.h"],
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "gpr_base",
        "json",
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
