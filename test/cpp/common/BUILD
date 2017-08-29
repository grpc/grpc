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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_test(
    name = "alarm_cpp_test",
    srcs = ["alarm_cpp_test.cc"],
    deps = [
        "//:grpc++_unsecure",
        "//test/core/util:gpr_test_util",
    ],
    external_deps = [
        "gtest",
    ],
)

grpc_cc_test(
    name = "auth_property_iterator_test",
    srcs = ["auth_property_iterator_test.cc"],
    deps = [
        "//:grpc++",
        "//test/core/util:gpr_test_util",
        "//test/cpp/util:test_util",
    ],
    external_deps = [
        "gtest",
    ],
)

grpc_cc_test(
    name = "channel_arguments_test",
    srcs = ["channel_arguments_test.cc"],
    deps = [
        "//:grpc++",
        "//test/core/util:gpr_test_util",
    ],
    external_deps = [
        "gtest",
    ],
)

grpc_cc_test(
    name = "channel_filter_test",
    srcs = ["channel_filter_test.cc"],
    deps = [
        "//:grpc++",
        "//test/core/util:gpr_test_util",
    ],
    external_deps = [
        "gtest",
    ],
)

grpc_cc_test(
    name = "secure_auth_context_test",
    srcs = ["secure_auth_context_test.cc"],
    deps = [
        "//:grpc++",
        "//test/core/util:gpr_test_util",
        "//test/cpp/util:test_util",
    ],
    external_deps = [
        "gtest",
    ],
)
