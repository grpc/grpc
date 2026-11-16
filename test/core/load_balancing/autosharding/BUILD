# Copyright 2026 gRPC authors.
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
    "grpc_cc_proto_library",
    "grpc_cc_test",
    "grpc_internal_proto_library",
    "grpc_package",
)
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(
    name = "test/core/load_balancing/autosharding",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "autosharding_assignment_test",
    srcs = ["autosharding_assignment_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:autosharding_cc_proto",
        "//:autosharding_upb",
        "//:grpc++_codegen_proto",
        "//:ref_counted_ptr",
        "//src/core:grpc_lb_policy_autosharding_assignment",
        "//test/core/test_util:grpc_test_util",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//upb/mem",
    ],
)

grpc_cc_test(
    name = "autosharding_client_test",
    srcs = ["autosharding_client_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:autosharding_cc_proto",
        "//:grpc",
        "//:grpc++_codegen_proto",
        "//:iomgr_timer",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
        "//src/core:grpc_lb_policy_autosharding_assignment",
        "//src/core:grpc_lb_policy_autosharding_client",
        "//src/core:time",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/xds:xds_transport_fake",
    ],
)

grpc_internal_proto_library(
    name = "autosharding_client_fuzzer_proto",
    srcs = [
        "autosharding_client_fuzzer.proto",
    ],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "@autosharding//:autosharding_proto",
    ],
)

grpc_cc_proto_library(
    name = "autosharding_client_fuzzer_cc_proto",
    deps = [
        "autosharding_client_fuzzer_proto",
    ],
)

grpc_fuzz_test(
    name = "autosharding_client_fuzzer",
    srcs = ["autosharding_client_fuzzer.cc"],
    external_deps = [
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        ":autosharding_client_fuzzer_cc_proto",
        "//:gpr",
        "//:grpc",
        "//:iomgr_timer",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
        "//src/core:grpc_check",
        "//src/core:grpc_lb_policy_autosharding_assignment",
        "//src/core:grpc_lb_policy_autosharding_client",
        "//src/core:time",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/xds:xds_transport_fake",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_cc_test(
    name = "autosharding_config_test",
    srcs = ["autosharding_config_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = [
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:config",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:down_cast",
        "//src/core:grpc_lb_policy_autosharding",
        "//src/core:json_reader",
        "//src/core:lb_policy",
        "//src/core:lb_policy_registry",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "autosharding_test",
    srcs = ["autosharding_test.cc"],
    external_deps = [
        "gtest",
        "absl/container:flat_hash_set",
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
        "//:autosharding_cc_proto",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc++_codegen_proto",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
        "//:xds_transport",
        "//src/core:channel_args",
        "//src/core:channel_factory",
        "//src/core:grpc_check",
        "//src/core:grpc_lb_policy_autosharding",
        "//src/core:grpc_lb_policy_autosharding_client",
        "//src/core:grpc_lb_policy_weighted_target",
        "//src/core:json",
        "//src/core:lb_policy",
        "//src/core:time",
        "//test/core/load_balancing:lb_policy_test_lib",
        "//test/core/test_util:grpc_test_util",
        "//test/core/xds:xds_transport_fake",
    ],
)
