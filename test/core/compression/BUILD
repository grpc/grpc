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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_package(name = "test/core/compression")

licenses(["notice"])  # Apache v2

grpc_cc_test(
    name = "algorithm_test",
    srcs = ["algorithm_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "compression_test",
    srcs = ["compression_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "message_compress_fuzzer",
    srcs = ["message_compress_fuzzer.cc"],
    corpus = "message_compress_corpus",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "message_decompress_fuzzer",
    srcs = ["message_decompress_fuzzer.cc"],
    corpus = "message_decompress_corpus",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "message_compress_test",
    srcs = ["message_compress_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "stream_compression_fuzzer",
    srcs = ["stream_compression_fuzzer.cc"],
    corpus = "stream_compression_corpus",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "stream_decompression_fuzzer",
    srcs = ["stream_decompression_fuzzer.cc"],
    corpus = "stream_decompression_corpus",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stream_compression_test",
    srcs = ["stream_compression_test.cc"],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
