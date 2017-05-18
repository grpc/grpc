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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

licenses(["notice"])  # 3-clause BSD

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
    name = "ssl_server_fuzzer",
    srcs = ["ssl_server_fuzzer.c"],
    language = "C",
    corpus = "corpus",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "oauth2_utils",
    srcs = ["oauth2_utils.c"],
    hdrs = ["oauth2_utils.h"],
    language = "C",
    deps = ["//:grpc"],
    visibility = ["//test/cpp:__subpackages__"],
)

grpc_cc_test(
    name = "auth_context_test",
    srcs = ["auth_context_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "credentials_test",
    srcs = ["credentials_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "secure_endpoint_test",
    srcs = ["secure_endpoint_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/iomgr:endpoint_tests",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "security_connector_test",
    srcs = ["security_connector_test.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "create_jwt",
    srcs = ["create_jwt.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
    ],
)

grpc_cc_binary(
    name = "fetch_oauth2",
    srcs = ["fetch_oauth2.c"],
    language = "C",
    deps = [
        ":oauth2_utils",
        "//:gpr",
        "//:grpc",
    ],
)

grpc_cc_binary(
    name = "verify_jwt",
    srcs = ["verify_jwt.c"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
    ],
)
