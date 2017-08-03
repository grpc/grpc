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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_cc_library")

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_test(
    name = "noop-benchmark",
    srcs = ["noop-benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
)

grpc_cc_library(
    name = "helpers",
    srcs = ["helpers.cc"],
    hdrs = [
        "fullstack_context_mutators.h",
        "fullstack_fixtures.h",
        "helpers.h",
    ],
    deps = [
        "//:grpc++_unsecure",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util_unsecure",
    ],
    external_deps = [
        "benchmark",
    ],
)

grpc_cc_test(
    name = "bm_closure",
    srcs = ["bm_closure.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_cq",
    srcs = ["bm_cq.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_cq_multiple_threads",
    srcs = ["bm_cq_multiple_threads.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_error",
    srcs = ["bm_error.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_streaming_ping_pong",
    srcs = ["bm_fullstack_streaming_ping_pong.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_streaming_pump",
    srcs = ["bm_fullstack_streaming_pump.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_trickle",
    srcs = ["bm_fullstack_trickle.cc"],
    deps = [":helpers"],
    external_deps = [
        "gflags",
    ],
)

grpc_cc_test(
    name = "bm_fullstack_unary_ping_pong",
    srcs = ["bm_fullstack_unary_ping_pong.cc"],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_metadata",
    srcs = ["bm_metadata.cc"],
    deps = [":helpers"],
)
