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
    name = "test/core/transport/binder/end2end",
    visibility = "tests",
)

grpc_cc_library(
    name = "fake_binder",
    testonly = 1,
    srcs = ["fake_binder.cc"],
    hdrs = ["fake_binder.h"],
    external_deps = [
        "absl/memory",
        "absl/random",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:variant",
    ],
    deps = [
        "//:gpr",
        "//:grpc++_binder",
    ],
)

grpc_cc_test(
    name = "fake_binder_test",
    srcs = ["fake_binder_test.cc"],
    external_deps = [
        "absl/strings",
        "absl/time",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":fake_binder",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "end2end_binder_channel",
    testonly = 1,
    srcs = ["testing_channel_create.cc"],
    hdrs = ["testing_channel_create.h"],
    external_deps = [],
    deps = [
        ":fake_binder",
        "//:grpc++_base",
        "//:grpc++_binder",
        "//:grpc_base",
        "//src/core:channel_args",
    ],
)

grpc_cc_test(
    name = "end2end_binder_transport_test",
    srcs = ["end2end_binder_transport_test.cc"],
    external_deps = [
        "absl/memory",
        "absl/time",
        "gtest",
    ],
    flaky = True,
    language = "C++",
    tags = [
        # Flaky on windows
        "no_windows",
        "no_mac",
        "no_test_ios",

        # Known race between stream creation and cancellation
        "notsan",
    ],
    deps = [
        ":end2end_binder_channel",
        ":fake_binder",
        "//:grpc++_binder",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)

grpc_cc_test(
    name = "binder_server_test",
    srcs = ["binder_server_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++",
        "//:grpc++_binder",
        "//test/core/transport/binder/end2end:fake_binder",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)
