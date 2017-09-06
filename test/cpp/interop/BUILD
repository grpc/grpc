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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary", "grpc_package")

grpc_package(name = "test/cpp/interop")

grpc_cc_library(
    name = "server_helper_lib",
    srcs = [
        "server_helper.cc",
    ],
    hdrs = [
        "server_helper.h",
    ],
    external_deps = [
        "gflags",
    ],
    language = "C++",
    deps = [
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_binary(
    name = "interop_server",
    srcs = [
        "interop_server_bootstrap.cc",
    ],
    language = "C++",
    deps = [
        ":interop_server_lib",
        "//:grpc++",
    ],
)

grpc_cc_library(
    name = "interop_server_lib",
    srcs = [
        "interop_server.cc",
    ],
    language = "C++",
    deps = [
        ":server_helper_lib",
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_library(
    name = "client_helper_lib",
    srcs = [
        "client_helper.cc",
        "interop_client.cc",
    ],
    hdrs = [
        "client_helper.h",
        "interop_client.h",
    ],
    language = "C++",
    deps = [
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/core/security:oauth2_utils",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "interop_client_main",
    srcs = [
        "client.cc",
    ],
    language = "C++",
    deps = [
        ":client_helper_lib",
    ],
)

grpc_cc_binary(
    name = "interop_client",
    language = "C++",
    deps = [
        ":interop_client_main",
        "//:grpc++",
    ],
)

grpc_cc_binary(
    name = "reconnect_interop_client",
    srcs = [
        "reconnect_interop_client.cc",
    ],
    deps = [
        ":client_helper_lib",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "reconnect_interop_server",
    srcs = [
        "reconnect_interop_server.cc",
    ],
    language = "C++",
    deps = [
        ":interop_server_lib",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "stress_test",
    srcs = [
        "stress_interop_client.cc",
        "stress_interop_client.h",
        "stress_test.cc",
    ],
    deps = [
        ":client_helper_lib",
        "//:grpc++",
        "//test/cpp/util:metrics_server_lib",
        "//test/cpp/util:test_config",
    ],
)
