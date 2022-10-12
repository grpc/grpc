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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_proto_library")

def grpc_fuzzer(name, corpus, srcs = [], tags = [], external_deps = [], deps = [], data = [], size = "large", **kwargs):
    """Instantiates a fuzzer test.

    Args:
        name: The name of the test.
        corpus: The corpus for the test.
        srcs: The source files for the test.
        external_deps: External deps.
        deps: The dependencies of the test.
        data: The data dependencies of the test.
        size: The size of the test.
        tags: The tags for the test.
        **kwargs: Other arguments to supply to the test.
    """
    CORPUS_DIR = native.package_name() + "/" + corpus
    grpc_cc_test(
        name = name,
        srcs = srcs,
        tags = tags + ["grpc-fuzzer", "no-cache"],
        deps = deps + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/util:fuzzer_corpus_test"],
        }),
        data = data + native.glob([corpus + "/**"]),
        external_deps = external_deps + [
            "gtest",
        ],
        size = size,
        args = select({
            "//:grpc_build_fuzzers": [CORPUS_DIR, "-runs=20000", "-max_total_time=300"],
            "//conditions:default": ["--directory=" + CORPUS_DIR],
        }),
        **kwargs
    )

def grpc_proto_fuzzer(name, corpus, proto, proto_deps = [], external_deps = [], srcs = [], tags = [], deps = [], data = [], size = "large", **kwargs):
    """Instantiates a protobuf mutator fuzzer test.

    Args:
        name: The name of the test.
        corpus: The corpus for the test.
        proto: The proto for the test.
        proto_deps: Deps for proto.
        external_deps: External deps.
        srcs: The source files for the test.
        deps: The dependencies of the test.
        data: The data dependencies of the test.
        size: The size of the test.
        tags: The tags for the test.
        **kwargs: Other arguments to supply to the test.
    """
    PROTO_LIBRARY = "_%s_proto" % name
    CORPUS_DIR = native.package_name() + "/" + corpus

    grpc_proto_library(
        name = PROTO_LIBRARY,
        srcs = [proto],
        deps = proto_deps,
        has_services = False,
    )

    grpc_cc_test(
        name = name,
        srcs = srcs,
        tags = tags + ["grpc-fuzzer", "no-cache"],
        deps = deps + [
            "@com_google_libprotobuf_mutator//:libprotobuf_mutator",
            PROTO_LIBRARY,
        ] + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/util:fuzzer_corpus_test"],
        }),
        data = data + native.glob([corpus + "/**"]),
        external_deps = external_deps + [
            "gtest",
        ],
        size = size,
        args = select({
            "//:grpc_build_fuzzers": [CORPUS_DIR, "-runs=20000", "-max_total_time=300"],
            "//conditions:default": ["--directory=" + CORPUS_DIR],
        }),
        **kwargs
    )
