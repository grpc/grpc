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

"""Generates the appropriate build.json data for all the bad_client tests."""

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test")

def test_options():
    return struct()

# maps test names to options
BAD_CLIENT_TESTS = {
    "badreq": test_options(),
    "bad_streaming_id": test_options(),
    "connection_prefix": test_options(),
    "duplicate_header": test_options(),
    "headers": test_options(),
    "initial_settings_frame": test_options(),
    "head_of_line_blocking": test_options(),
    "large_metadata": test_options(),
    "out_of_bounds": test_options(),
    "server_registered_method": test_options(),
    "simple_request": test_options(),
    "window_overflow": test_options(),
    "unknown_frame": test_options(),
}

def grpc_bad_client_tests():
    grpc_cc_library(
        name = "bad_client_test",
        srcs = ["bad_client.cc"],
        hdrs = ["bad_client.h"],
        language = "C++",
        deps = ["//test/core/util:grpc_test_util", "//:grpc", "//:gpr", "//test/core/end2end:cq_verifier"],
    )
    for t, topt in BAD_CLIENT_TESTS.items():
        grpc_cc_test(
            name = "%s_bad_client_test" % t,
            srcs = ["tests/%s.cc" % t],
            deps = [":bad_client_test"],
            external_deps = [
                "gtest",
            ],
        )
