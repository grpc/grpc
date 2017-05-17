# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_build_system.bzl", "grpc_proto_library")

grpc_proto_library(
    name = "compiler_test_proto",
    srcs = ["compiler_test.proto"],
    generate_mock = True,
)

grpc_proto_library(
    name = "control_proto",
    srcs = ["control.proto"],
    has_services = False,
    deps = [
        "payloads_proto",
        "stats_proto",
    ],
)

grpc_proto_library(
    name = "echo_messages_proto",
    srcs = ["echo_messages.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "echo_proto",
    srcs = ["echo.proto"],
    deps = ["echo_messages_proto"],
    generate_mock = True,
)

grpc_proto_library(
    name = "empty_proto",
    srcs = ["empty.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "messages_proto",
    srcs = ["messages.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "metrics_proto",
    srcs = ["metrics.proto"],
)

grpc_proto_library(
    name = "payloads_proto",
    srcs = ["payloads.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "services_proto",
    srcs = ["services.proto"],
    deps = [
        "control_proto",
        "messages_proto",
        "stats_proto",
    ],
)

grpc_proto_library(
    name = "stats_proto",
    srcs = ["stats.proto"],
    has_services = False,
)

grpc_proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    deps = [
        "empty_proto",
        "messages_proto",
    ],
)
