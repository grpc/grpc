# Copyright 2016 gRPC authors.
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
    name = "test/core/promise",
    visibility = "grpc",
)

grpc_cc_library(
    name = "poll",
    external_deps = [
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "poll.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "context",
    language = "c++",
    public_hdrs = [
        "context.h",
    ],
    deps = [
        "//:gpr_platform",
        "//:gpr_tls",
    ],
)

grpc_cc_library(
    name = "map",
    language = "c++",
    public_hdrs = ["map.h"],
    deps = [
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise",
    external_deps = [
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "promise.h",
    ],
    deps = [
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_like",
    language = "c++",
    public_hdrs = [
        "detail/promise_like.h",
    ],
    deps = [
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_factory",
    language = "c++",
    public_hdrs = [
        "detail/promise_factory.h",
    ],
    deps = [
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "if",
    external_deps = [
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = ["if.h"],
    deps = [
        "poll",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_status",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "detail/status.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "race",
    language = "c++",
    public_hdrs = ["race.h"],
    deps = [
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "loop",
    language = "c++",
    public_hdrs = [
        "loop.h",
    ],
    deps = [
        "poll",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "switch",
    language = "c++",
    public_hdrs = [
        "detail/switch.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "basic_join",
    language = "c++",
    public_hdrs = [
        "detail/basic_join.h",
    ],
    deps = [
        "poll",
        "promise_factory",
        "//:bitset",
        "//:construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "join",
    language = "c++",
    public_hdrs = [
        "join.h",
    ],
    deps = [
        "basic_join",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_join",
    language = "c++",
    public_hdrs = [
        "try_join.h",
    ],
    deps = [
        "basic_join",
        "promise_status",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "basic_seq",
    language = "c++",
    public_hdrs = [
        "detail/basic_seq.h",
    ],
    deps = [
        "poll",
        "promise_factory",
        "switch",
        "//:construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "seq",
    language = "c++",
    public_hdrs = [
        "seq.h",
    ],
    deps = [
        "basic_seq",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_seq",
    language = "c++",
    public_hdrs = [
        "try_seq.h",
    ],
    deps = [
        "basic_seq",
        "promise_status",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "activity",
    srcs = [
        "activity.cc",
    ],
    language = "c++",
    public_hdrs = [
        "activity.h",
    ],
    deps = [
        "context",
        "poll",
        "promise_factory",
        "promise_status",
        "//:atomic_utils",
        "//:construct_destruct",
        "//:gpr_base",
        "//:gpr_codegen",
    ],
)

grpc_cc_library(
    name = "wait_set",
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    language = "c++",
    public_hdrs = [
        "wait_set.h",
    ],
    deps = [
        "activity",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "intra_activity_waiter",
    language = "c++",
    public_hdrs = [
        "intra_activity_waiter.h",
    ],
    deps = [
        "activity",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "latch",
    external_deps = [
        "absl/status",
    ],
    language = "c++",
    public_hdrs = [
        "latch.h",
    ],
    deps = [
        "activity",
        "intra_activity_waiter",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "observable",
    language = "c++",
    public_hdrs = [
        "observable.h",
    ],
    deps = [
        "activity",
        "wait_set",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "pipe",
    external_deps = [
        "absl/status",
    ],
    language = "c++",
    public_hdrs = [
        "pipe.h",
    ],
    deps = [
        "activity",
        "intra_activity_waiter",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "for_each",
    external_deps = [
        "absl/status",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = ["for_each.h"],
    deps = [
        "poll",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_test(
    name = "poll_test",
    srcs = ["poll_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "poll",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "context_test",
    srcs = ["context_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "context",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "promise_test",
    srcs = ["promise_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "promise",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    # Why promise_map_test and not map_test?
    # third_party/benchmark defines a map_test in its cmakefile, and we depend
    # on that from our cmakefile, and cmake wants to have a flat namespace to
    # deal with xcode/visual studio limitations... sooo... promise_map_test it
    # is.
    name = "promise_map_test",
    srcs = ["map_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "map",
        "promise",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "race_test",
    srcs = ["race_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "race",
        "//test/core/util:grpc_suppressions",
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
        "promise",
        "promise_factory",
        "//:capture",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "if_test",
    srcs = ["if_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "if",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "loop_test",
    srcs = ["loop_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "loop",
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "join_test",
    srcs = ["join_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "join",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "try_join_test",
    srcs = ["try_join_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "try_join",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "seq_test",
    srcs = ["seq_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "try_seq_test",
    srcs = ["try_seq_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "try_seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "activity_test",
    srcs = ["activity_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "activity",
        "join",
        "promise",
        "seq",
        "wait_set",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "latch_test",
    srcs = ["latch_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "join",
        "latch",
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "observable_test",
    srcs = ["observable_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "observable",
        "promise",
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "for_each_test",
    srcs = ["for_each_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "for_each",
        "join",
        "map",
        "observable",
        "pipe",
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "pipe_test",
    srcs = ["pipe_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "join",
        "pipe",
        "promise",
        "seq",
        "//test/core/util:grpc_suppressions",
    ],
)
