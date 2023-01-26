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

grpc_package(name = "test/core/event_engine/test_suite/tools")

# -- Implementations --

grpc_cc_binary(
    name = "windows_event_engine_echo_client",
    testonly = True,
    srcs = ["windows_event_engine_factory.cc"],
    external_deps = ["absl/functional:any_invocable"],
    tags = [
        "bazel_only",
        "no_linux",
        "no_mac",
    ],
    deps = [
        "echo_client",
        "//:gpr",
        "//src/core:event_engine_interface",
        "//src/core:windows_event_engine",
    ],
)

# TODO(hork): this needs to reuse the same configuration in the test suite.
grpc_cc_binary(
    name = "posix_event_engine_echo_client",
    testonly = True,
    srcs = ["posix_event_engine_factory.cc"],
    external_deps = ["absl/functional:any_invocable"],
    tags = [
        "bazel_only",
        "no_windows",
    ],
    deps = [
        "echo_client",
        "//:gpr_platform",
        "//src/core:event_engine_interface",
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
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "C++",
    deps = [
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc",
        "//:grpc_resolver",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:default_event_engine",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:memory_quota",
        "//src/core:notification",
    ],
    alwayslink = True,
)
