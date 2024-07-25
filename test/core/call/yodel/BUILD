# Copyright 2024 gRPC authors.
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
    "grpc_package",
    "grpc_proto_library",
)

grpc_package(name = "test/core/call/yodel")

exports_files(
    ["fuzzer_main.cc"],
    visibility = ["//test:__subpackages__"],
)

grpc_cc_library(
    name = "yodel_test",
    testonly = 1,
    srcs = ["yodel_test.cc"],
    hdrs = ["yodel_test.h"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/strings",
        "gtest",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:config",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:iomgr_timer",
        "//:promise",
        "//src/core:call_arena_allocator",
        "//src/core:call_spine",
        "//src/core:cancel_callback",
        "//src/core:metadata",
        "//src/core:promise_factory",
        "//src/core:resource_quota",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "test_main",
    testonly = 1,
    srcs = ["test_main.cc"],
    external_deps = ["absl/random"],
    visibility = ["//test:__subpackages__"],
    deps = [
        "yodel_test",
        "//:grpc_trace",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_proto_library(
    name = "fuzzer_proto",
    srcs = ["fuzzer.proto"],
    has_services = False,
    visibility = ["//test:__subpackages__"],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
    ],
)
