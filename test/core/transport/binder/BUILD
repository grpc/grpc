# Copyright 2021 gRPC authors.
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
    name = "test/core/transport/binder",
    visibility = "tests",
)

grpc_cc_library(
    name = "mock_objects",
    testonly = 1,
    srcs = ["mock_objects.cc"],
    hdrs = ["mock_objects.h"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:grpc++_binder",
    ],
)

grpc_cc_test(
    name = "wire_writer_test",
    srcs = ["wire_writer_test.cc"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":mock_objects",
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "wire_reader_test",
    srcs = ["wire_reader_test.cc"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":mock_objects",
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "transport_stream_receiver_test",
    srcs = ["transport_stream_receiver_test.cc"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "binder_transport_test",
    srcs = ["binder_transport_test.cc"],
    external_deps = [
        "absl/memory",
        "absl/strings",
        "gtest",
    ],
    language = "C++",
    tags = [
        # To avoid `symbolizer buffer too small` warning of UBSAN
        "noubsan",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":mock_objects",
        "//:grpc",
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "endpoint_binder_pool_test",
    srcs = ["endpoint_binder_pool_test.cc"],
    external_deps = [
        "absl/memory",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":mock_objects",
        "//:grpc",
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
    ],
)
