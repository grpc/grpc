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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/cpp/common")

grpc_cc_test(
    name = "alarm_test",
    srcs = ["alarm_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["no_windows"],  # TODO(jtattermusch): fix test on windows RBE
    deps = [
        "//:grpc++_unsecure",
        "//src/core:notification",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "timer_test",
    srcs = ["timer_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "absl/log",
    ],
    flaky = True,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:iomgr",
        "//:iomgr_timer",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_jump_test",
    srcs = ["time_jump_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = [
        # Test manipulates system time and requires root while running so it is only
        # run on demand ("manual") and when no other tests are running ("exclusive").
        # It also means that the test won't work with setups like bazel RBE.
        "manual",
        "exclusive",
        "no_windows",
    ],
    deps = [
        "//:grpc++",
        "//src/core:closure",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "auth_property_iterator_test",
    srcs = ["auth_property_iterator_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:transport_auth_context",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "channel_arguments_test",
    srcs = ["channel_arguments_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:grpc",
        "//:grpc++",
        "//src/core:socket_mutator",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "secure_auth_context_test",
    srcs = ["secure_auth_context_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_security_base",
        "//:transport_auth_context",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "alts_util_test",
    srcs = ["alts_util_test.cc"],
    external_deps = [
        "gtest",
        "@com_google_protobuf//upb/mem",
    ],
    deps = [
        "//:alts_upb",
        "//:grpc++",
        "//:grpc++_alts",
        "//:grpc++_base",
        "//:tsi_alts_credentials",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
