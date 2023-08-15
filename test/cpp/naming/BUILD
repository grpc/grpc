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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_py_binary")
load(":generate_resolver_component_tests.bzl", "generate_resolver_component_tests")

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

licenses(["notice"])

# Meant to be invoked only through the top-level shell script driver.
grpc_py_binary(
    name = "resolver_component_tests_runner",
    testonly = True,
    srcs = [
        "resolver_component_tests_runner.py",
    ],
)

grpc_cc_test(
    name = "cancel_ares_query_test",
    srcs = ["cancel_ares_query_test.cc"],
    external_deps = ["gtest"],
    tags = ["cancel_ares_query_test"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:ares_resolver",
        "//src/core:channel_args",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:fake_udp_and_tcp_server",
        "//test/core/util:grpc_test_util",
        "//test/core/util:socket_use_after_close_detector",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

generate_resolver_component_tests()
