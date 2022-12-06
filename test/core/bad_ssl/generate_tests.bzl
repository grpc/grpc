#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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

"""
Houses grpc_bad_ssl_tests.
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test")

def test_options():
    return struct()

# maps test names to options
BAD_SSL_TESTS = ["cert", "alpn"]

# buildifier: disable=unnamed-macro
def grpc_bad_ssl_tests():
    """Instantiates gRPC bad SSL tests."""

    grpc_cc_library(
        name = "bad_ssl_test_server",
        srcs = ["server_common.cc"],
        hdrs = ["server_common.h"],
        deps = [
            "//test/core/util:grpc_test_util",
            "//test/core/util:grpc_test_util_base",
            "//:grpc",
        ],
    )
    for t in BAD_SSL_TESTS:
        grpc_cc_binary(
            name = "bad_ssl_%s_server" % t,
            srcs = ["servers/%s.cc" % t],
            deps = [":bad_ssl_test_server"],
        )
        grpc_cc_test(
            name = "bad_ssl_%s_test" % t,
            srcs = ["bad_ssl_test.cc"],
            data = [
                ":bad_ssl_%s_server" % t,
                "//src/core/tsi/test_creds:badserver.key",
                "//src/core/tsi/test_creds:badserver.pem",
                "//src/core/tsi/test_creds:ca.pem",
                "//src/core/tsi/test_creds:server1.key",
                "//src/core/tsi/test_creds:server1.pem",
            ],
            deps = [
                "//test/core/util:grpc_test_util",
                "//test/core/util:grpc_test_util_base",
                "//:gpr",
                "//:grpc",
                "//test/core/end2end:cq_verifier",
            ],
            tags = ["no_windows"],
        )
