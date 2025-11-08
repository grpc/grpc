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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/test_suite/tools",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

# -- Implementations --

grpc_cc_binary(
    name = "windows_event_engine_echo_client",
    testonly = True,
    srcs = ["windows_event_engine_factory.cc"],
    external_deps = [
        "absl/log:check",
        "absl/functional:any_invocable",
    ],
    tags = [
        "bazel_only",
        "grpc:event_engine_impl",
        "no_linux",
        "no_mac",
    ],
    deps = [
        "echo_client",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//src/core:grpc_check",
        "//src/core:windows_event_engine",
    ],
)

# TODO(hork): this needs to reuse the same configuration in the test suite.
grpc_cc_binary(
    name = "posix_event_engine_echo_client",
    testonly = True,
    srcs = ["posix_event_engine_factory.cc"],
    external_deps = [
        "absl/log:check",
        "absl/functional:any_invocable",
    ],
    tags = [
        "bazel_only",
        "grpc:event_engine_impl",
        "no_windows",
    ],
    deps = [
        "echo_client",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//src/core:grpc_check",
        "//src/core:iomgr_port",
        "//src/core:posix_event_engine",
    ],
)

# -- Testing tools --

grpc_cc_library(
    name = "echo_client",
    testonly = True,
    srcs = ["echo_client.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/log:check",
        "absl/log:log",
        "absl/functional:any_invocable",
        "absl/strings:str_format",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "//:config",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc_resolver",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:common_event_engine_closures",
        "//src/core:default_event_engine",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:grpc_check",
        "//src/core:memory_quota",
        "//src/core:notification",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)
