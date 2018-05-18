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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/cpp/client")

grpc_cc_test(
    name = "credentials_test",
    srcs = ["credentials_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
    ],
)

grpc_cc_test(
    name = "client_channel_stress_test",
    srcs = ["client_channel_stress_test.cc"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/lb/v1:load_balancer_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
    ],
)
