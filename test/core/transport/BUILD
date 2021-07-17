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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/transport")

grpc_cc_test(
    name = "bdp_estimator_test",
    srcs = ["bdp_estimator_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = ["no_windows"],  # TODO(jtattermusch): investigate the timeout on windows
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "byte_stream_test",
    srcs = ["byte_stream_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "connectivity_state_test",
    srcs = ["connectivity_state_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "error_utils_test",
    srcs = ["error_utils_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "pid_controller_test",
    srcs = ["pid_controller_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "static_metadata_test",
    srcs = ["static_metadata_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_conversion_test",
    srcs = ["status_conversion_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stream_owned_slice_test",
    srcs = ["stream_owned_slice_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "timeout_encoding_test",
    srcs = ["timeout_encoding_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_metadata_test",
    srcs = ["status_metadata_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
