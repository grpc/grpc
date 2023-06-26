# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/ext/filters/otel",
    visibility = "tests",
)

grpc_cc_test(
    name = "otel_plugin_test",
    srcs = [
        "otel_plugin_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
    ],
    language = "C++",
    linkstatic = True,
    tags = [
    ],
    deps = ["//test/core/util:grpc_test_util"],
)
