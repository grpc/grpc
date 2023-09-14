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
    "grpc_cc_test",
    "grpc_package",
)

grpc_package(name = "test/core/client_channel/lb_policy")

licenses(["notice"])

grpc_cc_library(
    name = "lb_policy_test_lib",
    testonly = True,
    hdrs = ["lb_policy_test_lib.h"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//src/core:lb_policy",
        "//src/core:subchannel_interface",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
    ],
)

grpc_cc_test(
    name = "pick_first_test",
    srcs = ["pick_first_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//src/core:channel_args",
        "//src/core:grpc_lb_policy_pick_first",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "round_robin_test",
    srcs = ["round_robin_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//src/core:channel_args",
        "//src/core:grpc_lb_policy_round_robin",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "outlier_detection_lb_config_parser_test",
    srcs = ["outlier_detection_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "outlier_detection_test",
    srcs = ["outlier_detection_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "lb_unit_test",
    ],
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//src/core:channel_args",
        "//src/core:grpc_lb_policy_outlier_detection",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_lb_config_parser_test",
    srcs = ["xds_override_host_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_test",
    srcs = ["xds_override_host_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "lb_unit_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//src/core:grpc_lb_policy_xds_override_host",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "rls_lb_config_parser_test",
    srcs = ["rls_lb_config_parser_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "static_stride_scheduler_test",
    srcs = ["static_stride_scheduler_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = ["lb_unit_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:static_stride_scheduler",
    ],
)

grpc_cc_test(
    name = "static_stride_scheduler_benchmark",
    srcs = ["static_stride_scheduler_benchmark.cc"],
    external_deps = [
        "absl/algorithm:container",
        "benchmark",
    ],
    language = "C++",
    tags = [
        "lb_unit_test",
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:no_destruct",
        "//src/core:static_stride_scheduler",
    ],
)

grpc_cc_test(
    name = "weighted_round_robin_config_test",
    srcs = ["weighted_round_robin_config_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "lb_unit_test",
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "weighted_round_robin_test",
    srcs = ["weighted_round_robin_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "lb_unit_test",
    ],
    uses_polling = False,
    deps = [
        ":lb_policy_test_lib",
        "//src/core:grpc_lb_policy_weighted_round_robin",
        "//test/core/util:grpc_test_util",
    ],
)
