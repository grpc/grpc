# Copyright 2022 gRPC authors.
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
    "grpc_cc_proto_library",
    "grpc_cc_test",
    "grpc_internal_proto_library",
    "grpc_package",
)
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

grpc_package(
    name = "test/core/load_balancing",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_library(
    name = "lb_policy_test_lib",
    testonly = True,
    hdrs = ["lb_policy_test_lib.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/types:span",
        "gtest",
    ],
    deps = [
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_client_channel",
        "//:grpc_security_base",
        "//:iomgr_timer",
        "//:oob_backend_metric",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:uri",
        "//:work_serializer",
        "//src/core:channel_args",
        "//src/core:client_channel_internal_header",
        "//src/core:connectivity_state",
        "//src/core:default_event_engine",
        "//src/core:experiments",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_check",
        "//src/core:grpc_service_config",
        "//src/core:health_check_client",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:match",
        "//src/core:resolved_address",
        "//src/core:subchannel_interface",
        "//src/core:subchannel_pool_interface",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:unique_type_name",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_cc_test(
    name = "pick_first_test",
    srcs = ["pick_first_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/strings",
        "absl/synchronization",
        "absl/types:span",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
        "//src/core:channel_args",
        "//src/core:experiments",
        "//src/core:grpc_lb_policy_pick_first",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:metrics",
        "//src/core:time",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
    ],
)

grpc_internal_proto_library(
    name = "pick_first_fuzzer_proto",
    srcs = [
        "pick_first_fuzzer.proto",
    ],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

grpc_cc_proto_library(
    name = "pick_first_fuzzer_cc_proto",
    deps = [
        "pick_first_fuzzer_proto",
    ],
)

grpc_fuzz_test(
    name = "pick_first_fuzzer",
    srcs = ["pick_first_fuzzer.cc"],
    external_deps = [
        "absl/log:log",
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    tags = ["no_windows"],
    deps = [
        ":pick_first_fuzzer_cc_proto",
        "//:config",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:grpc",
        "//:grpc++_config_proto",
        "//:grpc_client_channel",
        "//:iomgr_timer",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:uri",
        "//:work_serializer",
        "//src/core:channel_args",
        "//src/core:client_channel_internal_header",
        "//src/core:connectivity_state",
        "//src/core:grpc_check",
        "//src/core:grpc_service_config",
        "//src/core:health_check_client",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:match",
        "//src/core:resolved_address",
        "//src/core:subchannel_interface",
        "//src/core:subchannel_pool_interface",
        "//src/core:time",
        "//src/core:unique_type_name",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_cc_test(
    name = "random_subsetting_test",
    srcs = ["random_subsetting_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:config",
        "//:endpoint_addresses",
        "//:gpr",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_test(
    name = "round_robin_test",
    srcs = ["round_robin_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/strings",
        "absl/types:span",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:endpoint_addresses",
        "//:grpc",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:experiments",
        "//src/core:grpc_lb_policy_round_robin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "outlier_detection_lb_config_parser_test",
    srcs = ["outlier_detection_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "outlier_detection_test",
    srcs = ["outlier_detection_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/strings",
        "absl/types:span",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_lb_policy_outlier_detection",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_lb_config_parser_test",
    srcs = ["xds_override_host_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:client_channel_service_config",
        "//src/core:grpc_lb_policy_xds_override_host",
        "//src/core:grpc_service_config",
        "//src/core:xds_health_status",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_test",
    srcs = ["xds_override_host_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_lb_policy_xds_override_host",
        "//src/core:grpc_stateful_session_filter",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:xds_config",
        "//src/core:xds_health_status",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "rls_lb_config_parser_test",
    srcs = ["rls_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:client_channel_service_config",
        "//src/core:down_cast",
        "//src/core:grpc_lb_policy_rls",
        "//src/core:grpc_service_config",
        "//src/core:json_writer",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "static_stride_scheduler_test",
    srcs = ["static_stride_scheduler_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:static_stride_scheduler",
    ],
)

grpc_cc_benchmark(
    name = "static_stride_scheduler_benchmark",
    srcs = ["static_stride_scheduler_benchmark.cc"],
    external_deps = [
        "absl/algorithm:container",
        "absl/random",
        "absl/types:span",
    ],
    monitoring = HISTORY,
    uses_event_engine = False,
    deps = [
        "//src/core:grpc_check",
        "//src/core:no_destruct",
        "//src/core:static_stride_scheduler",
    ],
)

grpc_cc_test(
    name = "weighted_round_robin_config_test",
    srcs = ["weighted_round_robin_config_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "weighted_round_robin_test",
    srcs = ["weighted_round_robin_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/strings",
        "absl/time",
        "absl/types:span",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//src/core:grpc_backend_metric_data",
        "//src/core:grpc_lb_policy_weighted_round_robin",
        "//src/core:grpc_lb_policy_weighted_target",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/core:lb_policy",
        "//src/core:time",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ring_hash_test",
    srcs = ["ring_hash_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/strings",
    ],
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_lb_policy_ring_hash",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:xxhash_inline",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
    ],
)

grpc_cc_benchmark(
    name = "bm_picker",
    srcs = ["bm_picker.cc"],
    external_deps = [
        "absl/strings",
    ],
    monitoring = HISTORY,
    deps = [
        "//:config",
        "//:grpc",
        "//:grpc_client_channel",
        "//:parse_address",
        "//src/core:channel_args_endpoint_config",
        "//src/core:connectivity_state",
        "//src/core:default_event_engine",
        "//src/core:health_check_client",
        "//src/core:json_reader",
        "//src/core:lb_policy",
        "//test/core/test_util:build",
    ],
)
