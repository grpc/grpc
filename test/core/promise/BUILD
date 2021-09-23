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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/core/promise")

load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_cc_test(
    name = "poll_test",
    srcs = ["poll_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_polling = False,
    deps = [
        "//src/core/lib/promise:poll",
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
        "//src/core/lib/promise:context",
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
        "//src/core/lib/promise",
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
        "//src/core/lib/promise",
        "//src/core/lib/promise:map",
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
        "//src/core/lib/promise:race",
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
        "//:capture",
        "//src/core/lib/promise",
        "//src/core/lib/promise:promise_factory",
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
        "//src/core/lib/promise:if",
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
        "//src/core/lib/promise:loop",
        "//src/core/lib/promise:seq",
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
        "//src/core/lib/promise:join",
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
        "//src/core/lib/promise:try_join",
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
        "//src/core/lib/promise:seq",
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
        "//src/core/lib/promise:try_seq",
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
        "//src/core/lib/promise",
        "//src/core/lib/promise:activity",
        "//src/core/lib/promise:join",
        "//src/core/lib/promise:seq",
        "//src/core/lib/promise:wait_set",
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
        "//src/core/lib/promise:join",
        "//src/core/lib/promise:latch",
        "//src/core/lib/promise:seq",
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
        "//src/core/lib/promise",
        "//src/core/lib/promise:observable",
        "//src/core/lib/promise:seq",
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
        "//src/core/lib/promise:for_each",
        "//src/core/lib/promise:join",
        "//src/core/lib/promise:map",
        "//src/core/lib/promise:observable",
        "//src/core/lib/promise:pipe",
        "//src/core/lib/promise:seq",
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
        "//src/core/lib/promise",
        "//src/core/lib/promise:join",
        "//src/core/lib/promise:pipe",
        "//src/core/lib/promise:seq",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_proto_fuzzer(
    name = "promise_fuzzer",
    srcs = ["promise_fuzzer.cc"],
    corpus = "promise_fuzzer_corpus",
    language = "C++",
    proto = "promise_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//test/core/util:grpc_test_util",
    ],
)
