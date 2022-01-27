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

"""
Includes fuzzer rules.
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

def grpc_fuzzer(name, corpus, srcs = [], deps = [], data = [], size = "large", **kwargs):
    """Instantiates a fuzzer test.

    Args:
        name: The name of the test.
        corpus: The corpus for the test.
        srcs: The source files for the test.
        deps: The dependencies of the test.
        data: The data dependencies of the test.
        size: The size of the test.
        **kwargs: Other arguments to supply to the test.
    """
    CORPUS_DIR = native.package_name() + "/" + corpus
    grpc_cc_test(
        name = name,
        srcs = srcs,
        deps = deps + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/util:fuzzer_corpus_test"],
        }),
        data = data + native.glob([corpus + "/**"]),
        external_deps = [
            "gtest",
        ],
        size = size,
        args = select({
            "//:grpc_build_fuzzers": [CORPUS_DIR],
            "//conditions:default": ["--directory=" + CORPUS_DIR],
        }),
        **kwargs
    )

def grpc_proto_fuzzer(name, corpus, proto, srcs = [], deps = [], data = [], size = "large", **kwargs):
    """Instantiates a protobuf mutator fuzzer test.

    Args:
        name: The name of the test.
        corpus: The corpus for the test.
        proto: The proto for the test.
        srcs: The source files for the test.
        deps: The dependencies of the test.
        data: The data dependencies of the test.
        size: The size of the test.
        **kwargs: Other arguments to supply to the test.
    """
    PROTO_LIBRARY = "_%s_proto" % name
    CC_PROTO_LIBRARY = "_%s_cc_proto" % name
    CORPUS_DIR = native.package_name() + "/" + corpus

    proto_library(
        name = PROTO_LIBRARY,
        srcs = [proto],
    )

    cc_proto_library(
        name = CC_PROTO_LIBRARY,
        deps = [PROTO_LIBRARY],
    )

    grpc_cc_test(
        name = name,
        srcs = srcs,
        deps = deps + [
            "@com_google_libprotobuf_mutator//:libprotobuf_mutator",
            CC_PROTO_LIBRARY,
        ] + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/util:fuzzer_corpus_test"],
        }),
        data = data + native.glob([corpus + "/**"]),
        external_deps = [
            "gtest",
        ],
        size = size,
        args = select({
            "//:grpc_build_fuzzers": [CORPUS_DIR],
            "//conditions:default": ["--directory=" + CORPUS_DIR],
        }),
        **kwargs
    )
