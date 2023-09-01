# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/test_suite/tests",
    visibility = "public",
)

# TODO(hork): A single InitTestSuite() call should initialize all tests.
# This can be done after each test suite specialization can explicitly opt in or out
# of tests declaratively.
# grpc_cc_library(
#     name = "init",
#     testonly = True,
#     hdrs = ["init.h"],
#     deps = [
#         "client",
#         "dns",
#         "server",
#         "timer",
#     ],
#     alwayslink = 1,
# )

grpc_cc_library(
    name = "timer",
    testonly = True,
    srcs = ["timer_test.cc"],
    hdrs = ["timer_test.h"],
    deps = [
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite:event_engine_test_framework",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "dns",
    testonly = True,
    srcs = ["dns_test.cc"],
    hdrs = ["dns_test.h"],
    data = [
        "dns_test_record_groups.yaml",
        "//test/cpp/naming/utils:dns_resolver",
        "//test/cpp/naming/utils:dns_server",
        "//test/cpp/naming/utils:health_check",
        "//test/cpp/naming/utils:tcp_connect",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "address_sorting",
    ],
    deps = [
        "//:subprocess",
        "//src/core:env",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite:event_engine_test_framework",
        "//test/core/util:fake_udp_and_tcp_server",
        "//test/core/util:grpc_test_util_base",
        "//test/cpp/util:get_grpc_test_runfile_dir",
        "//test/cpp/util:test_util",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "client",
    testonly = True,
    srcs = ["client_test.cc"],
    hdrs = ["client_test.h"],
    deps = [
        "//src/core:channel_args",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite:event_engine_test_framework",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "server",
    testonly = True,
    srcs = ["server_test.cc"],
    hdrs = ["server_test.h"],
    deps = [
        "//src/core:channel_args",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite:event_engine_test_framework",
    ],
    alwayslink = 1,
)
