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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

grpc_cc_test(
    name = "activity_test",
    srcs = ["activity_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:activity",
        "//:join",
        "//:promise",
        "//:seq",
        "//:wait_set",
    ],
)

grpc_cc_test(
    name = "if_test",
    srcs = ["if_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:if"],
)

grpc_cc_test(
    name = "poll_test",
    srcs = ["poll_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:poll"],
)

grpc_cc_test(
    name = "for_each_test",
    srcs = ["for_each_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:for_each",
        "//:join",
        "//:map",
        "//:observable",
        "//:pipe",
        "//:seq",
    ],
)

grpc_cc_test(
    name = "observable_test",
    srcs = ["observable_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:observable",
        "//:promise",
        "//:seq",
    ],
)

grpc_cc_test(
    name = "pipe_test",
    srcs = ["pipe_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:join",
        "//:pipe",
        "//:promise",
        "//:seq",
    ],
)

grpc_cc_test(
    name = "promise_factory_test",
    srcs = ["promise_factory_test.cc"],
    external_deps = [
        "gtest",
        "absl/functional:bind_front",
    ],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:capture",
        "//:promise",
        "//:promise_factory",
    ],
)

grpc_cc_test(
    name = "context_test",
    srcs = ["context_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:context"],
)

grpc_cc_test(
    name = "promise_test",
    srcs = ["promise_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:promise"],
)

grpc_cc_test(
    name = "seq_test",
    srcs = ["seq_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:seq"],
)

grpc_cc_test(
    name = "try_seq_test",
    srcs = ["try_seq_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:try_seq"],
)

grpc_cc_test(
    name = "join_test",
    srcs = ["join_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:join"],
)

grpc_cc_test(
    name = "try_join_test",
    srcs = ["try_join_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:try_join"],
)

grpc_cc_test(
    name = "race_test",
    srcs = ["race_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:race"],
)

grpc_cc_test(
    name = "visitor_test",
    srcs = ["visitor_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:visitor"],
)

grpc_cc_test(
    name = "loop_test",
    srcs = ["loop_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = ["//:loop"],
)

grpc_cc_test(
    name = "latch_test",
    srcs = ["latch_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:join",
        "//:latch",
        "//:seq",
    ],
)
