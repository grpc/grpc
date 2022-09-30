# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine",
    visibility = "tests",
)

grpc_cc_test(
    name = "common_closures_test",
    srcs = ["common_closures_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "gtest",
    ],
    deps = [
        "//:common_event_engine_closures",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:notification",
    ],
)

grpc_cc_test(
    name = "forkable_test",
    srcs = ["forkable_test.cc"],
    external_deps = [
        "absl/time",
        "gtest",
    ],
    deps = [
        "//:forkable",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_unsecure",
    ],
)

grpc_cc_test(
    name = "thread_pool_test",
    srcs = ["thread_pool_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:event_engine_thread_pool",
        "//:gpr",
        "//:notification",
    ],
)

grpc_cc_test(
    name = "endpoint_config_test",
    srcs = ["endpoint_config_test.cc"],
    external_deps = [
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr_platform",
        "//:grpc",
    ],
)

grpc_cc_test(
    name = "default_engine_methods_test",
    srcs = ["default_engine_methods_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/time",
        "gtest",
    ],
    deps = [
        "//:default_event_engine",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "smoke_test",
    srcs = ["smoke_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "aborting_event_engine",
        "//:default_event_engine",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "slice_buffer_test",
    srcs = ["slice_buffer_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:event_engine_common",
        "//:gpr",
        "//:gpr_platform",
        "//:slice",
    ],
)

grpc_cc_library(
    name = "test_init",
    srcs = ["test_init.cc"],
    hdrs = ["test_init.h"],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "aborting_event_engine",
    testonly = True,
    hdrs = ["util/aborting_event_engine.h"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)
