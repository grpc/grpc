# Copyright 2016, Google Inc.
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

load(":generate_tests.bzl", "grpc_end2end_tests")

grpc_cc_library(
    name = "cq_verifier",
    srcs = ["cq_verifier.c"],
    hdrs = ["cq_verifier.h"],
    language = "C",
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "ssl_test_data",
    srcs = [
        "data/client_certs.c",
        "data/server1_cert.c",
        "data/server1_key.c",
        "data/test_root_cert.c",
    ],
    hdrs = ["data/ssl_test_data.h"],
    language = "C",
    visibility = ["//test:__subpackages__"],
)

grpc_cc_library(
    name = "fake_resolver",
    srcs = ["fake_resolver.c"],
    hdrs = ["fake_resolver.h"],
    language = "C",
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "http_proxy",
    srcs = ["fixtures/http_proxy_fixture.c"],
    hdrs = ["fixtures/http_proxy_fixture.h"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "proxy",
    srcs = ["fixtures/proxy.c"],
    hdrs = ["fixtures/proxy.h"],
    language = "C",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_end2end_tests()
