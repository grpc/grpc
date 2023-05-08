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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

licenses(["notice"])

grpc_package(name = "test/core/filters")

grpc_cc_library(
    name = "filter_test",
    testonly = 1,
    srcs = ["filter_test.cc"],
    hdrs = ["filter_test.h"],
    external_deps = [
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "gtest",
    ],
    language = "c++",
    tags = ["nofixdeps"],  # until event engine tests are under fixbuilddeps
    deps = [
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:basic_seq",
        "//src/core:context",
        "//src/core:memory_quota",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
    ],
)

grpc_cc_test(
    name = "filter_test_test",
    srcs = ["filter_test_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        "//:grpc_unsecure",
        "//src/core:activity",
        "//src/core:arena_promise",
        "//src/core:map",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:seq",
        "//src/core:slice",
    ],
)

grpc_cc_test(
    name = "client_auth_filter_test",
    srcs = ["client_auth_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "gtest",
    ],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:promise",
        "//:ref_counted_ptr",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:grpc_fake_credentials",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_cc_test(
    name = "client_authority_filter_test",
    srcs = ["client_authority_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/types:optional",
        "gtest",
    ],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        "//:grpc",
        "//src/core:grpc_client_authority_filter",
    ],
)

grpc_proto_fuzzer(
    name = "filter_fuzzer",
    srcs = ["filter_fuzzer.cc"],
    corpus = "filter_fuzzer_corpus",
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "C++",
    proto = "filter_fuzzer.proto",
    # TODO(ctiller): remove noautofuzz tag when this fuzzer is ready to be load bearing.
    tags = [
        "no_windows",
        "noautofuzz",
    ],
    uses_polling = False,
    deps = [
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_http_filters",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:handshaker",
        "//:iomgr_timer",
        "//:legacy_context",
        "//:ref_counted_ptr",
        "//:tsi_base",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:context",
        "//src/core:env",
        "//src/core:error",
        "//src/core:grpc_authorization_base",
        "//src/core:grpc_channel_idle_filter",
        "//src/core:grpc_client_authority_filter",
        "//src/core:iomgr_fwd",
        "//src/core:memory_quota",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:time",
        "//src/core:transport_fwd",
        "//src/core:useful",
    ],
)
