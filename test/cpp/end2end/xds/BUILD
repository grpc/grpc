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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/end2end/xds",
    visibility = "public",
)  # Allows external users to implement end2end tests.

grpc_cc_library(
    name = "xds_server",
    hdrs = ["xds_server.h"],
    srcs = ["xds_server.cc"],
    external_deps = [
        "gtest",
    ],
    testonly = True,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds:ads_for_test_proto",
        "//src/proto/grpc/testing/xds:lrs_for_test_proto",
        "//src/proto/grpc/testing/xds/v3:ads_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:lrs_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//test/core/util:grpc_test_util",
    ],
)
