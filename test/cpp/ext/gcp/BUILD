# Copyright 2022 gRPC authors.
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

grpc_package(name = "test/cpp/ext/gcp")

grpc_cc_test(
    name = "observability_test",
    srcs = [
        "observability_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:grpcpp_gcp_observability",
        "//src/proto/grpc/testing:echo_proto",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "observability_config_test",
    srcs = [
        "observability_config_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//src/cpp/ext/gcp:observability_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "observability_logging_sink_test",
    srcs = [
        "observability_logging_sink_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//src/cpp/ext/gcp:observability_logging_sink",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "environment_autodetect_test",
    srcs = [
        "environment_autodetect_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    linkstatic = True,
    deps = [
        "//src/cpp/ext/gcp:environment_autodetect",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)
