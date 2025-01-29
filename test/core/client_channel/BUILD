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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

grpc_package(name = "test/core/client_channel")

licenses(["notice"])

grpc_cc_test(
    name = "subchannel_args_test",
    srcs = ["subchannel_args_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc_client_channel",
        "//src/core:channel_args",
        "//src/core:subchannel_pool_interface",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "client_channel_test",
    srcs = ["client_channel_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//:grpc_client_channel",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "connected_subchannel_test",
    srcs = ["connected_subchannel_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//:grpc_client_channel",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "load_balanced_call_destination_test",
    srcs = ["load_balanced_call_destination_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//:grpc_client_channel",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "retry_interceptor_test",
    srcs = ["retry_interceptor_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//src/core:retry_interceptor",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_test(
    name = "retry_throttle_test",
    srcs = ["retry_throttle_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "client_channel_service_config_test",
    srcs = ["client_channel_service_config_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "retry_service_config_test",
    srcs = ["retry_service_config_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_client_channel",
    srcs = ["bm_client_channel.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//src/core:default_event_engine",
        "//test/core/transport:call_spine_benchmarks",
    ],
)

grpc_cc_benchmark(
    name = "bm_load_balanced_call_destination",
    srcs = ["bm_load_balanced_call_destination.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//src/core:default_event_engine",
        "//test/core/transport:call_spine_benchmarks",
    ],
)

grpc_cc_test(
    name = "lb_metadata_test",
    srcs = ["lb_metadata_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:lb_metadata",
        "//src/core:metadata_batch",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "retry_state_test",
    srcs = ["retry_state_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:json_reader",
        "//src/core:retry_interceptor",
    ],
)
