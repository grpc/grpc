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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test")

licenses(["notice"])

grpc_cc_test(
    name = "iocp_test",
    timeout = "short",
    srcs = ["iocp_test.cc"],
    external_deps = [
        "gtest",
        "absl/types:variant",
    ],
    language = "C++",
    tags = [
        "no_linux",
        "no_mac",
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "create_sockpair",
        "//:gpr_platform",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_utils",
        "//src/core:windows_iocp",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "win_socket_test",
    timeout = "short",
    srcs = ["win_socket_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_linux",
        "no_mac",
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "create_sockpair",
        "//:gpr_platform",
        "//src/core:common_event_engine_closures",
        "//src/core:windows_event_engine",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "windows_endpoint_test",
    timeout = "short",
    srcs = ["windows_endpoint_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_linux",
        "no_mac",
        "no_test_ios",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "create_sockpair",
        "//:gpr_platform",
        "//src/core:common_event_engine_closures",
        "//src/core:windows_endpoint",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "create_sockpair",
    srcs = ["create_sockpair.cc"],
    hdrs = ["create_sockpair.h"],
    external_deps = ["absl/status"],
    language = "C++",
    tags = [
        "no_linux",
        "no_mac",
    ],
    deps = [
        "//:gpr_platform",
        "//:grpc_base",
    ],
)
