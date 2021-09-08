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
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "client_channel_stress_test",
    srcs = ["client_channel_stress_test.cc"],
    # TODO(jtattermusch): test fails frequently on Win RBE, but passes locally
    # reenable the tests once it works reliably on Win RBE.
    # TODO(roth): Test marked as manual for now due to variable duration
    # problem triggered by https://github.com/grpc/grpc/pull/22481.
    # Once we figure out the problem, either re-enable or just decide to
    # remove this test.  Tracked internally in b/153136407.
    tags = [
        "manual",
        "no_test_android",  # fails on android due to "Too many open files".
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/lb/v1:load_balancer_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "destroy_grpclb_channel_with_active_connect_stress_test",
    srcs = ["destroy_grpclb_channel_with_active_connect_stress_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
