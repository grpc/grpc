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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/end2end",
    visibility = "public",
)  # Allows external users to implement end2end tests.

grpc_cc_library(
    name = "test_service_impl",
    testonly = True,
    srcs = ["test_service_impl.cc"],
    hdrs = ["test_service_impl.h"],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/log:check",
        "absl/synchronization",
    ],
    visibility = ["//bazel:end2end_test_utils"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:grpc_check",
        "//src/core:notification",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "test_health_check_service_impl",
    testonly = True,
    srcs = ["test_health_check_service_impl.cc"],
    hdrs = ["test_health_check_service_impl.h"],
    deps = [
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/health/v1:health_cc_grpc",
    ],
)

grpc_cc_library(
    name = "counted_service",
    hdrs = ["counted_service.h"],
    deps = [
        "//:grpc",
        "//src/core:sync",
    ],
)

grpc_cc_library(
    name = "connection_attempt_injector",
    testonly = True,
    srcs = ["connection_attempt_injector.cc"],
    hdrs = ["connection_attempt_injector.h"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/memory",
        "absl/utility",
    ],
    deps = [
        "//:exec_ctx",
        "//:grpc",
        "//:iomgr",
        "//:sockaddr_utils",
        "//src/core:channel_args_endpoint_config",
        "//src/core:default_event_engine",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "interceptors_util",
    testonly = True,
    srcs = ["interceptors_util.cc"],
    hdrs = ["interceptors_util.h"],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "absl/strings:str_format",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc++",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "rls_server",
    testonly = True,
    srcs = ["rls_server.cc"],
    hdrs = ["rls_server.h"],
    external_deps = [
        "gtest",
        "absl/log",
    ],
    deps = [
        ":counted_service",
        "//src/core:time",
        "//src/proto/grpc/lookup/v1:rls_cc_grpc",
        "//src/proto/grpc/lookup/v1:rls_cc_proto",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "async_end2end_test",
    srcs = ["async_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    shard_count = 10,
    tags = [
        "cpp_end2end_test",
        "no_test_ios",
        "thready_tsan",
    ],
    deps = [
        "//:config_vars",
        "//:debug_location",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:iomgr",
        "//src/core:client_channel_backup_poller",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/proto/grpc/health/v1:health_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:build",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "time_change_test",
    srcs = ["time_change_test.cc"],
    data = [
        ":client_crash_test_server",
    ],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//:iomgr_timer",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "latent_see_service_test",
    srcs = ["latent_see_service_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_latent_see_client",
        "//:grpcpp_latent_see_service",
        "//src/core:json_reader",
        "//src/proto/grpc/channelz/v2:latent_see_cc_grpc",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_crash_test",
    srcs = ["client_crash_test.cc"],
    data = [
        ":client_crash_test_server",
    ],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "grpc:fails-internally",
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_binary(
    name = "client_crash_test_server",
    testonly = True,
    srcs = ["client_crash_test_server.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/log",
        "gtest",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_fork_test",
    srcs = ["client_fork_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    tags = [
        "fork_test",
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "client_callback_end2end_test",
    srcs = ["client_callback_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_codegen_proto",
        "//:iomgr",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing:simple_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "delegating_channel_test",
    srcs = ["delegating_channel_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_codegen_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_interceptors_end2end_test",
    srcs = ["client_interceptors_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_codegen_proto",
        "//:iomgr",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "end2end_test_lib",
    testonly = True,
    srcs = ["end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "gtest",
    ],
    linkstatic = True,
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_test",
        "//:grpc_security_base",
        "//:iomgr",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
    alwayslink = 1,
)

grpc_cc_test(
    name = "channelz_service_test",
    srcs = ["channelz_service_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    # TODO(yulin-liang): The test is not able to load the certificate files on
    # iOS. Figure out why.
    tags = [
        "cpp_end2end_test",
        # b/117517566.
        "grpc:no-internal-poller",
        "no_test_ios",
        "channelz_test",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_channelz",
        "//:tsi_ssl_credentials",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_tls_credentials",
        "//src/core:slice",
        "//src/core:wait_for_single_owner",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "channelz_v2_service_test",
    srcs = ["channelz_v2_service_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    # TODO(yulin-liang): The test is not able to load the certificate files on
    # iOS. Figure out why.
    tags = [
        "cpp_end2end_test",
        # b/117517566.
        "grpc:no-internal-poller",
        "no_test_ios",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_channelz",
        "//:tsi_ssl_credentials",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:grpc_tls_credentials",
        "//src/core:slice",
        "//src/core:wait_for_single_owner",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:service_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_early_return_test",
    srcs = ["server_early_return_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "end2end_test",
    size = "large",
    flaky = True,  # TODO(b/151704375)
    shard_count = 30,
    tags = [
        "cpp_end2end_test",
        "no_test_ios",
    ],
    deps = [
        ":end2end_test_lib",
        # DO NOT REMOVE THE grpc++ dependence below since the internal build
        # system uses it to specialize targets
        "//:grpc++",
    ],
)

grpc_cc_test(
    name = "exception_test",
    srcs = ["exception_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "generic_end2end_test",
    srcs = ["generic_end2end_test.cc"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_codegen_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "health_service_end2end_test",
    srcs = ["health_service_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        # Will fail as sync health check service is enabled by default internally, which is however
        # disabled in OSS:
        # b/117623310.
        "grpc:no-internal-poller",
    ],
    deps = [
        ":test_health_check_service_impl",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/health/v1:health_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "hybrid_end2end_test",
    srcs = ["hybrid_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:iomgr",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "raw_end2end_test",
    srcs = ["raw_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "mock_test",
    srcs = ["mock_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_test",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_cc_test(
    name = "nonblocking_test",
    srcs = ["nonblocking_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/memory",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:iomgr",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_lb_end2end_test",
    srcs = ["client_lb_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    flaky = True,  # TODO(b/151315347)
    tags = [
        "cpp_end2end_test",
        "cpp_lb_end2end_test",
        # b/117575949.
        "grpc:no-internal-poller",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":connection_attempt_injector",
        ":test_service_impl",
        "//:backoff",
        "//:config_vars",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_client_channel",
        "//:grpc_resolver_fake",
        "//:grpc_service_config_impl",
        "//:grpcpp_backend_metric_recorder",
        "//:grpcpp_call_metric_recorder",
        "//:grpcpp_orca_service",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:server",
        "//:sockaddr_utils",
        "//src/core:channel_args",
        "//src/core:client_channel_backup_poller",
        "//src/core:config_selector",
        "//src/core:connectivity_state",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_service_config",
        "//src/core:notification",
        "//src/core:time",
        "//src/proto/grpc/health/v1:health_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:postmortem",
        "//test/core/test_util:test_lb_policies",
        "//test/cpp/util:test_util",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "rls_end2end_test",
    srcs = ["rls_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    flaky = True,
    tags = [
        "cpp_end2end_test",
        "no_test_ios",
    ],
    deps = [
        ":counted_service",
        ":rls_server",
        ":test_service_impl",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_core_credentials_header",
        "//:grpc_resolver_fake",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:parse_address",
        "//:uri",
        "//src/core:channel_args",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_lb_policy_rls",
        "//src/core:grpc_sockaddr",
        "//src/core:time",
        "//src/core:wait_for_single_owner",
        "//src/proto/grpc/lookup/v1:rls_cc_grpc",
        "//src/proto/grpc/lookup/v1:rls_cc_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:test_call_creds",
        "//test/core/test_util:test_lb_policies",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "service_config_end2end_test",
    srcs = ["service_config_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/memory",
        "absl/strings",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:backoff",
        "//:config_vars",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_client_channel",
        "//:grpc_resolver_fake",
        "//:grpc_service_config_impl",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:client_channel_backup_poller",
        "//src/core:error_utils",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "grpclb_end2end_test",
    srcs = ["grpclb_end2end_test.cc"],
    external_deps = [
        "absl/cleanup",
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/types:span",
        "gtest",
    ],
    flaky = True,  # TODO(b/150567713)
    shard_count = 20,
    tags = [
        "cpp_end2end_test",
        "cpp_lb_end2end_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":counted_service",
        ":test_service_impl",
        "//:config_vars",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_core_credentials_header",
        "//:grpc_grpclb_balancer_addresses",
        "//:grpc_resolver_fake",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_lb_policy_grpclb",
        "//src/core:sync",
        "//src/proto/grpc/lb/v1:load_balancer_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:test_call_creds",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "proto_server_reflection_test",
    srcs = ["proto_server_reflection_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        # b/117623310.
        "grpc:no-internal-poller",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_reflection",
        "//src/proto/grpc/reflection/v1:reflection_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:grpc++_proto_reflection_desc_db",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_builder_plugin_test",
    srcs = ["server_builder_plugin_test.cc"],
    external_deps = [
        "gtest",
        "absl/memory",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_crash_test",
    srcs = ["server_crash_test.cc"],
    data = [
        ":server_crash_test_client",
    ],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "grpc:fails-internally",
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_binary(
    name = "server_crash_test_client",
    testonly = True,
    srcs = ["server_crash_test_client.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/log:check",
        "absl/log",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_interceptors_end2end_test",
    srcs = ["server_interceptors_end2end_test.cc"],
    external_deps = [
        "absl/memory",
        "absl/strings",
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
    ],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_codegen_proto",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_load_reporting_end2end_test",
    srcs = ["server_load_reporting_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        # TODO(b/264692543): Re-enable after fixing
        "grpc:broken-internally",
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:grpcpp_server_load_reporting",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "flaky_network_test",
    srcs = ["flaky_network_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        # Test manipulates network settings (e.g. using iptables) while running so it is only
        # run on demand ("manual") and when no other tests are running ("exclusive").
        # It also means that the test won't work with setups like bazel RBE.
        "manual",
        "exclusive",
        "no_test_ios",
        # Needs root (not available internally).
        "grpc:fails-internally",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "shutdown_test",
    srcs = ["shutdown_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "streaming_throughput_test",
    srcs = ["streaming_throughput_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
    ],
    tags = [
        "cpp_end2end_test",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "thread_stress_test",
    size = "large",
    srcs = ["thread_stress_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
    ],
    shard_count = 5,
    tags = [
        "cpp_end2end_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:env",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "cfstream_test",
    srcs = ["cfstream_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        # Test requires root and manipulates network settings while running so it is only
        # run on demand ("manual") and when no other tests are running ("exclusive").
        # It also means that the test won't work with setups like bazel RBE.
        "manual",
        "exclusive",
        "no_test_android",
        "no_test_ios",
        # Needs root (not available internally).
        "grpc:fails-internally",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing:simple_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "message_allocator_end2end_test",
    srcs = ["message_allocator_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        # This test only works with the open source protobuf.
        "grpc:broken-internally",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing:simple_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "context_allocator_end2end_test",
    srcs = ["context_allocator_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:iomgr",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing:simple_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "port_sharing_end2end_test",
    srcs = ["port_sharing_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:iomgr",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "admin_services_end2end_test",
    srcs = ["admin_services_end2end_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_admin",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "grpc_authz_end2end_test",
    srcs = ["grpc_authz_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client-with-spiffe.key",
        "//src/core/tsi/test_creds:client-with-spiffe.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_authorization_provider",
        "//:grpc++_base",
        "//:grpc_authorization_provider",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_fake_credentials",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:audit_logging_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "tls_key_export_test",
    srcs = ["tls_key_export_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server0.key",
        "//src/core/tsi/test_creds:server0.pem",
    ],
    external_deps = [
        "absl/log:check",
        "absl/strings",
        "gtest",
        "libssl",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "orca_service_end2end_test",
    srcs = ["orca_service_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/strings",
        "absl/time",
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_backend_metric_recorder",
        "//:grpcpp_call_metric_recorder",
        "//:grpcpp_orca_service",
        "//src/core:notification",
        "//src/core:time",
        "//src/proto/grpc/testing/xds/v3:orca_service_cc_grpc",
        "//src/proto/grpc/testing/xds/v3:orca_service_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "resource_quota_end2end_stress_test",
    srcs = ["resource_quota_end2end_stress_test.cc"],
    external_deps = [
        "gtest",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:experiments",
        "//src/core:grpc_fake_credentials",
        "//src/core:notification",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "ssl_credentials_test",
    srcs = ["ssl_credentials_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/synchronization",
    ],
    tags = ["ssl_credentials_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:postmortem",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "tls_credentials_test",
    srcs = ["tls_credentials_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/synchronization",
    ],
    tags = ["tls_credentials_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_security_base",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "crl_provider_test",
    srcs = ["crl_provider_test.cc"],
    data = [
        "//test/core/tsi/test_creds/crl_data:ca.pem",
        "//test/core/tsi/test_creds/crl_data:revoked.key",
        "//test/core/tsi/test_creds/crl_data:revoked.pem",
        "//test/core/tsi/test_creds/crl_data:valid.key",
        "//test/core/tsi/test_creds/crl_data:valid.pem",
        "//test/core/tsi/test_creds/crl_data/crls:current.crl",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
        "gtest",
    ],
    tags = ["crl_provider_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_security_base",
        "//src/core:grpc_check",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "spiffe_bundle_map_end2end_test",
    srcs = ["spiffe_bundle_map_end2end_test.cc"],
    data = [
        "//test/core/tsi/test_creds/spiffe_end2end:ca.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:client.key",
        "//test/core/tsi/test_creds/spiffe_end2end:client_spiffe.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:client_spiffebundle.json",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate.cnf",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_ca.key",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_ca.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_gen.sh",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_and_intermediate_chain.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_signed_by_intermediate.key",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_signed_by_intermediate.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:server.key",
        "//test/core/tsi/test_creds/spiffe_end2end:server_spiffe.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:server_spiffebundle.json",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
        "gtest",
    ],
    tags = ["crl_provider_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
