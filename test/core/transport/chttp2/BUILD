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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/transport/chttp2")

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")
load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")

grpc_fuzzer(
    name = "hpack_parser_fuzzer",
    srcs = ["hpack_parser_fuzzer_test.cc"],
    corpus = "hpack_parser_corpus",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "alpn_test",
    srcs = ["alpn_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bin_decoder_test",
    srcs = ["bin_decoder_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bin_encoder_test",
    srcs = ["bin_encoder_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "context_list_test",
    srcs = ["context_list_test.cc"],
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
    name = "hpack_encoder_test",
    srcs = ["hpack_encoder_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "hpack_parser_test",
    srcs = ["hpack_parser_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "hpack_table_test",
    srcs = ["hpack_table_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stream_map_test",
    srcs = ["stream_map_test.cc"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "settings_timeout_test",
    srcs = ["settings_timeout_test.cc"],
    external_deps = [
        "gtest",
    ],
    flaky = True,  # TODO(b/148399919)
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    "too_many_pings_test",
    timeout = "long",  # Required for internal test infrastructure (cl/325757166)
    srcs = ["too_many_pings_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "varint_test",
    srcs = ["varint_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "remove_stream_from_stalled_lists_test",
    srcs = ["remove_stream_from_stalled_lists_test.cc"],
    # use LARGE_MACHINE because this test needs a certaim amount
    # of parallelism in order to reproduce the original crash that it's meant
    # to regression test for (a crash which is fixed by
    # https://github.com/grpc/grpc/pull/23984). Experiments show that if this
    # test doesn't use LARGE_MACHINE, then it will almost never reproduce the
    # intended crash (as it's meant to before the fix is applied). But with
    # LARGE_MACHINE, it can reproduce at a high rate.
    exec_properties = LARGE_MACHINE,
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = ["no_windows"],  # LARGE_MACHINE is not configured for windows RBE
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
