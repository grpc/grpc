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
    name = "test/cpp/ext/gsm",
    visibility = "tests",
)

grpc_cc_test(
    name = "gsm_observability_test",
    srcs = [
        "gsm_observability_test.cc",
    ],
    external_deps = [
        "google_cloud_cpp:experimental-opentelemetry",
        "gtest",
        "otel/sdk/src/metrics",
    ],
    language = "C++",
    tags = [
    ],
    deps = [
        "//:grpc++",
        "//src/cpp/ext/gsm:gsm_observability",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "metadata_exchange_test",
    srcs = [
        "metadata_exchange_test.cc",
    ],
    external_deps = [
        "google_cloud_cpp:experimental-opentelemetry",
        "gtest",
        "otel/sdk/src/metrics",
    ],
    language = "C++",
    tags = [
    ],
    deps = [
        "//:grpc++",
        "//src/cpp/ext/gsm:gsm_observability",
        "//test/core/util:grpc_test_util",
        "//test/cpp/ext/otel:otel_test_library",
    ],
)

grpc_cc_test(
    name = "mesh_id_test",
    srcs = [
        "mesh_id_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:grpc++",
        "//src/cpp/ext/gsm:gsm_observability",
        "//test/core/util:grpc_test_util",
    ],
)
