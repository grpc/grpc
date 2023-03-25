# Copyright 2018 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/ext/filters/census",
    visibility = "tests",
)

grpc_cc_library(
    name = "library",
    testonly = 1,
    srcs = ["library.cc"],
    hdrs = ["library.h"],
    external_deps = [
        "gtest",
        "opencensus-stats-test",
        "opencensus-tags",
        "opencensus-with-tag-map",
    ],
    language = "C++",
    deps = [
        "//:grpc++",
        "//:grpc_opencensus_plugin",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:test_lb_policies",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "grpc_opencensus_plugin_test",
    srcs = [
        "stats_plugin_end2end_test.cc",
    ],
    external_deps = [
        "gtest",
        "opencensus-stats-test",
        "opencensus-tags",
        "opencensus-with-tag-map",
    ],
    flaky = True,
    language = "C++",
    linkstatic = True,
    tags = [
        "census_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        "library",
        "//:grpc++",
        "//:grpc_opencensus_plugin",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "constant_labels_test",
    srcs = [
        "constant_labels_test.cc",
    ],
    external_deps = [
        "gtest",
        "opencensus-stats-test",
        "opencensus-tags",
        "opencensus-with-tag-map",
    ],
    flaky = True,
    language = "C++",
    linkstatic = True,
    tags = [
        "census_test",
        "no_windows",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        "library",
        "//:grpc++",
        "//:grpc_opencensus_plugin",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)
