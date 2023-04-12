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
        "absl/synchronization",
    ],
    deps = [
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/health/v1:health_proto",
    ],
)

grpc_cc_library(
    name = "counted_service",
    testonly = True,
    hdrs = ["counted_service.h"],
    deps = [
        "//:grpc",
    ],
)

grpc_cc_library(
    name = "connection_attempt_injector",
    testonly = True,
    srcs = ["connection_attempt_injector.cc"],
    hdrs = ["connection_attempt_injector.h"],
    deps = [
        "//:grpc",
    ],
)

grpc_cc_library(
    name = "interceptors_util",
    testonly = True,
    srcs = ["interceptors_util.cc"],
    hdrs = ["interceptors_util.h"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//src/proto/grpc/testing:echo_proto",
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
    ],
    deps = [
        ":counted_service",
        "//src/proto/grpc/lookup/v1:rls_proto",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "async_end2end_test",
    srcs = ["async_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    shard_count = 10,
    tags = [
        "cpp_end2end_test",
        "no_test_ios",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/health/v1:health_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
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
        "gtest",
    ],
    tags = [
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_binary(
    name = "client_crash_test_server",
    testonly = True,
    srcs = ["client_crash_test_server.cc"],
    external_deps = [
        "absl/flags:flag",
        "gtest",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_callback_end2end_test",
    srcs = ["client_callback_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing:simple_messages_proto",
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_interceptors_end2end_test",
    srcs = ["client_interceptors_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "end2end_test_lib",
    testonly = True,
    srcs = ["end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    linkstatic = True,
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_test",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
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
        "gtest",
    ],
    # TODO(yulin-liang): The test is not able to load the certificate files on
    # iOS. Figure out why.
    tags = [
        "cpp_end2end_test",
        "no_test_ios",
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_channelz",
        "//src/core:slice",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "end2end_test",
    size = "large",
    flaky = True,  # TODO(b/151704375)
    shard_count = 10,
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "filter_end2end_test",
    srcs = ["filter_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "generic_end2end_test",
    srcs = ["generic_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "health_service_end2end_test",
    srcs = ["health_service_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_health_check_service_impl",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/health/v1:health_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "hybrid_end2end_test",
    srcs = ["hybrid_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "raw_end2end_test",
    srcs = ["raw_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "mock_test",
    srcs = ["mock_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_test",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "nonblocking_test",
    srcs = ["nonblocking_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "client_lb_end2end_test",
    srcs = ["client_lb_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    flaky = True,  # TODO(b/151315347)
    tags = [
        "cpp_end2end_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":connection_attempt_injector",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_backend_metric_recorder",
        "//:grpcpp_call_metric_recorder",
        "//:grpcpp_orca_service",
        "//src/core:channel_args",
        "//src/proto/grpc/health/v1:health_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//src/proto/grpc/testing/xds/v3:orca_load_report_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:test_lb_policies",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "rls_end2end_test",
    srcs = ["rls_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/types:optional",
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
        "//src/proto/grpc/lookup/v1:rls_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:test_lb_policies",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "service_config_end2end_test",
    srcs = ["service_config_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "grpclb_end2end_test",
    srcs = ["grpclb_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    flaky = True,  # TODO(b/150567713)
    tags = [
        "cpp_end2end_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":counted_service",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/core:channel_args",
        "//src/proto/grpc/lb/v1:load_balancer_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
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
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_reflection",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc++_proto_reflection_desc_db",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_builder_plugin_test",
    srcs = ["server_builder_plugin_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
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
        "gtest",
    ],
    tags = [
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_binary(
    name = "server_crash_test_client",
    testonly = True,
    srcs = ["server_crash_test_client.cc"],
    external_deps = [
        "absl/flags:flag",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "server_interceptors_end2end_test",
    srcs = ["server_interceptors_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":interceptors_util",
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
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
        "no_test_ios",
        "no_windows",
    ],
    deps = [
        "//:grpcpp_server_load_reporting",
        "//src/proto/grpc/testing:echo_proto",
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
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "shutdown_test",
    srcs = ["shutdown_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "streaming_throughput_test",
    srcs = ["streaming_throughput_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "cpp_end2end_test",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "thread_stress_test",
    size = "large",
    srcs = ["thread_stress_test.cc"],
    external_deps = [
        "gtest",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "cfstream_test",
    srcs = ["cfstream_test.cc"],
    external_deps = [
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
    ],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing:simple_messages_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "message_allocator_end2end_test",
    srcs = ["message_allocator_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing:simple_messages_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "context_allocator_end2end_test",
    srcs = ["context_allocator_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing:simple_messages_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "port_sharing_end2end_test",
    srcs = ["port_sharing_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "admin_services_end2end_test",
    srcs = ["admin_services_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:grpc++",
        "//:grpc++_reflection",
        "//:grpcpp_admin",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "grpc_authz_end2end_test",
    srcs = ["grpc_authz_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
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
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "orca_service_end2end_test",
    srcs = ["orca_service_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["cpp_end2end_test"],
    deps = [
        "//:grpc++",
        "//:grpcpp_backend_metric_recorder",
        "//:grpcpp_call_metric_recorder",
        "//:grpcpp_orca_service",
        "//src/proto/grpc/testing/xds/v3:orca_service_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
