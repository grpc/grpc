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
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

grpc_package(
    name = "test/core/telemetry",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "call_tracer_test",
    srcs = ["call_tracer_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:call_tracer",
        "//:event_engine_base_hdrs",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:context",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "metrics_test",
    srcs = ["metrics_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:channel_args_endpoint_config",
        "//src/core:metrics",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "stats_test",
    srcs = ["stats_test.cc"],
    external_deps = [
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:stats",
        "//src/core:stats_data",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "trace_flags_test",
    srcs = ["trace_flags_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_trace",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "instrument_test",
    srcs = ["instrument_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
        "absl/random",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:instrument",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_instrument",
    srcs = ["bm_instrument.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//src/core:instrument",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "histogram_fuzzer",
    srcs = ["histogram_fuzzer.cc"],
    external_deps = [
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//src/core:histogram",
    ],
)

grpc_cc_benchmark(
    name = "bm_histogram",
    srcs = ["bm_histogram.cc"],
    external_deps = [
        "absl/random",
    ],
    monitoring = HISTORY,
    deps = [
        "//src/core:histogram",
    ],
)
