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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/core/event_engine/test_suite/posix")

grpc_cc_library(
    name = "oracle_event_engine_posix",
    testonly = True,
    srcs = ["oracle_event_engine_posix.cc"],
    hdrs = ["oracle_event_engine_posix.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
    ],
    tags = [
        "cpu:10",
        "no_windows",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:gpr",
        "//:sockaddr_utils",
        "//src/core:event_engine_interface",
        "//src/core:event_engine_memory_allocator",
        "//src/core:event_engine_slice_buffer",
        "//src/core:notification",
        "//src/core:resolved_address",
        "//src/core:strerror",
        "//test/core/event_engine:event_engine_test_utils",
    ],
)

grpc_cc_test(
    name = "oracle_event_engine_posix_test",
    srcs = ["oracle_event_engine_posix_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = [
        "no_test_ios",
        "no_windows",
    ],
    # TODO(vignesbabu): This is required because the oracle event engine uses
    # poll syscall. If uses_polling is set to False, there will be an attempt
    # to run this test with GRPC_POLL_STRATEGY=none which will hijack the poll
    # c-wrapper causing the test to fail. A more generic posix oracle event
    # engine design which doesn't rely on poll is required.
    uses_polling = True,
    deps = [
        "oracle_event_engine_posix",
        "//test/core/event_engine/test_suite:event_engine_test_framework",
        "//test/core/util:grpc_test_util",
    ],
)
