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
    python_version = "PY3",
)

grpc_cc_test(
    name = "cancel_ares_query_test",
    srcs = ["cancel_ares_query_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        "cancel_ares_query_test",
        "grpc:fails-internally",
        # b/117575742.
        "grpc:no-internal-poller",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:ares_resolver",
        "//src/core:channel_args",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:fake_udp_and_tcp_server",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:socket_use_after_close_detector",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

filegroup(
    name = "resolver_test_record_groups",
    srcs = ["resolver_test_record_groups.yaml"],
)

generate_resolver_component_tests()
