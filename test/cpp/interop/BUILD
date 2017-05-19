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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

grpc_cc_library(
    name = "server_helper_lib",
    language = "c++",
    srcs = [
        "server_helper.cc",
    ],
    hdrs = [
        "server_helper.h",
    ],
    deps = [
        "//test/cpp/util:test_util",
    ],
    external_deps = [
        "gflags",
    ],
)

grpc_cc_binary(
    name = "interop_server",
    language = "c++",
    srcs = [
        "interop_server.cc",
        "interop_server_bootstrap.cc",
    ],
    deps = [
        ":server_helper_lib",
        "//:grpc++",
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_library(
    name = "client_helper_lib",
    language = "c++",
    srcs = [
        "client_helper.cc",
        "interop_client.cc",
    ],
    hdrs = [
        "client_helper.h",
        "interop_client.h",
    ],
    deps = [
        "//test/cpp/util:test_util",
        "//src/proto/grpc/testing:empty_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:test_proto",
        "//test/core/security:oauth2_utils",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_library(
    name = "interop_client_main",
    language = "c++",
    srcs = [
        "client.cc",
    ],
    deps = [
        ":client_helper_lib"
    ],
)

grpc_cc_binary(
    name = "interop_client",
    language = "c++",
    deps = [
        ":interop_client_main",
    ],
)
