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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

grpc_package(
    name = "test/cpp/end2end",
    visibility = "public",
)  # Allows external users to implement end2end tests.

grpc_cc_library(
    name = "test_service_impl",
    testonly = True,
    srcs = ["test_service_impl.cc"],
    hdrs = ["test_service_impl.h"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//src/proto/grpc/testing:echo_proto",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "test_health_check_service_impl",
    testonly = True,
    srcs = ["test_health_check_service_impl.cc"],
    hdrs = ["test_health_check_service_impl.h"],
    deps = [
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/health/v1:health_proto",
    ],
)

grpc_cc_library(
    name = "interceptors_util",
    testonly = True,
    srcs = ["interceptors_util.cc"],
    hdrs = ["interceptors_util.h"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//src/proto/grpc/testing:echo_proto",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "xds_end2end_test",
    size = "large",
    srcs = ["xds_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    shard_count = 10,
    tags = [
        "no_test_ios",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":test_service_impl",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//src/proto/grpc/testing/xds:ads_for_test_proto",
        "//src/proto/grpc/testing/xds:cds_for_test_proto",
        "//src/proto/grpc/testing/xds:eds_for_test_proto",
        "//src/proto/grpc/testing/xds:lds_rds_for_test_proto",
        "//src/proto/grpc/testing/xds:lrs_for_test_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

