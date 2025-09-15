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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/channelz")

licenses(["notice"])

grpc_cc_test(
    name = "channel_trace_test",
    srcs = ["channel_trace_test.cc"],
    external_deps = [
        "gtest",
        "absl/synchronization",
    ],
    tags = [
        # b/117517566.
        "grpc:no-internal-poller",
        "channelz_test",
    ],
    deps = [
        "//:channelz",
        "//:channelz_upb",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/core:upb_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:channel_trace_proto_helper",
    ],
)

grpc_cc_test(
    name = "property_list_test",
    srcs = ["property_list_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
        "@com_google_protobuf//upb/mem",
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "@com_google_protobuf//:duration_upb_proto",
        # TODO(rishesh): Re-enable this once the proto is available in OSS.
        # "//google/protobuf:timestamp_upb_proto",
        "//:channelz_property_list_upb",
        "//src/core:channelz_property_list",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/core:upb_utils",
    ],
)

grpc_cc_test(
    name = "channelz_test",
    srcs = ["channelz_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/status:statusor",
    ],
    tags = [
        # b/117517566.
        "grpc:no-internal-poller",
        "channelz_test",
    ],
    deps = [
        "//:channel",
        "//:channelz",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_core_credentials_header",
        "//:server",
        "//src/core:channel_args",
        "//src/core:channelz_v2tov1_legacy_api",
        "//src/core:default_event_engine",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core:notification",
        "//src/core:upb_utils",
        "//src/core:useful",
        "//src/core:wait_for_single_owner",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:channel_trace_proto_helper",
    ],
)

grpc_cc_test(
    name = "channelz_registry_test",
    srcs = ["channelz_registry_test.cc"],
    external_deps = [
        "gtest",
    ],
    shard_count = 25,
    tags = [
        "channelz_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:channelz",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:notification",
        "//src/core:shared_bit_gen",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ztrace_collector_test",
    srcs = ["ztrace_collector_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    tags = [
        "channelz_test",
        "no_windows",
    ],
    deps = [
        "//:grpc",
        "//:grpc++",
        "//src/core:notification",
        "//src/core:ztrace_collector",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//src/proto/grpc/channelz/v2:service_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "channel_trace_fuzzer",
    srcs = ["channel_trace_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = [
        "channelz_test",
    ],
    deps = [
        "//:channelz",
        "//src/core:match",
    ],
)

grpc_fuzz_test(
    name = "channelz_fuzzer",
    srcs = ["channelz_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = [
        "channelz_test",
    ],
    deps = [
        "//:channelz",
    ],
)
