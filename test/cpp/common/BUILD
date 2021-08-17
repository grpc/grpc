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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

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
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "timer_test",
    srcs = ["timer_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "time_jump_test",
    srcs = ["time_jump_test.cc"],
    external_deps = [
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "auth_property_iterator_test",
    srcs = ["auth_property_iterator_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_polling = False,
    deps = [
        "//:grpc++",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "channel_arguments_test",
    srcs = ["channel_arguments_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_polling = False,
    deps = [
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_filter_test",
    srcs = ["channel_filter_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_polling = False,
    deps = [
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "secure_auth_context_test",
    srcs = ["secure_auth_context_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "alts_util_test",
    srcs = ["alts_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++_alts",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)
