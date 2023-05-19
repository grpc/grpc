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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/test_suite",
    visibility = "public",
)

grpc_cc_library(
    name = "event_engine_test_framework",
    testonly = True,
    srcs = ["event_engine_test_framework.cc"],
    hdrs = ["event_engine_test_framework.h"],
    external_deps = ["gtest"],
    deps = [
        "//:grpc",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/util:grpc_test_util",
    ],
)

# -- EventEngine implementations --

grpc_cc_test(
    name = "posix_event_engine_test",
    srcs = ["posix_event_engine_test.cc"],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//src/core:posix_event_engine",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/event_engine/test_suite/tests:client",
        "//test/core/event_engine/test_suite/tests:server",
        "//test/core/event_engine/test_suite/tests:timer",
    ],
)

grpc_cc_test(
    name = "thready_posix_event_engine_test",
    srcs = ["thready_posix_event_engine_test.cc"],
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = True,
    deps = [
        "//src/core:posix_event_engine",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/event_engine/test_suite/tests:client",
        "//test/core/event_engine/test_suite/tests:server",
        "//test/core/event_engine/test_suite/tests:timer",
        "//src/core:thready_event_engine",
    ],
)

grpc_cc_test(
    name = "windows_event_engine_test",
    srcs = ["windows_event_engine_test.cc"],
    tags = [
        "bazel_only",
        "no_linux",
        "no_mac",
    ],
    uses_polling = False,
    deps = [
        "//src/core:windows_event_engine",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/test_suite/tests:client",
        "//test/core/event_engine/test_suite/tests:server",
        "//test/core/event_engine/test_suite/tests:timer",
    ],
)

grpc_cc_test(
    name = "cf_event_engine_test",
    srcs = ["cf_event_engine_test.cc"],
    tags = [
        "no_linux",
        "no_windows",
    ],
    uses_polling = True,
    deps = [
        "//src/core:cf_event_engine",
        "//test/core/event_engine/test_suite/posix:oracle_event_engine_posix",
        "//test/core/event_engine/test_suite/tests:client",
        "//test/core/event_engine/test_suite/tests:timer",
    ],
)

grpc_cc_test(
    name = "fuzzing_event_engine_test",
    srcs = ["fuzzing_event_engine_test.cc"],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/test_suite/tests:client",
        "//test/core/event_engine/test_suite/tests:server",
        "//test/core/event_engine/test_suite/tests:timer",
    ],
)
