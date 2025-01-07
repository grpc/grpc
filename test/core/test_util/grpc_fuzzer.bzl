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

Now that we are at C++17, please prefer grpc_fuzz_test over the
grpc_fuzzer/grpc_proto_fuzzer older rules for new fuzzers - the former is
simpler and better maintained, and we'll eventually replace existing fuzzers
with grpc_fuzz_test.
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library")

def grpc_fuzzer(name, corpus, owner = "grpc", srcs = [], tags = [], external_deps = [], deps = [], data = [], size = "large", **kwargs):
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
        owner: The owning team of the test (for auto-bug-filing).
        **kwargs: Other arguments to supply to the test.
    """
    CORPUS_DIR = native.package_name() + "/" + corpus
    grpc_cc_test(
        name = name,
        srcs = srcs,
        tags = tags + ["grpc-fuzzer", "no-cache"],
        deps = deps + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/test_util:fuzzer_corpus_test"],
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

def grpc_proto_fuzzer(
        name,
        corpus,
        proto,
        owner = "grpc",  # @unused
        proto_deps = [],
        external_deps = [],
        srcs = [],
        tags = [],
        deps = [],
        end2end_fuzzer = False,  # @unused
        data = [],
        size = "large",
        **kwargs):
    """Instantiates a protobuf mutator fuzzer test.

    Args:
        name: The name of the test.
        corpus: The corpus for the test.
        proto: The proto for the test. If empty, it assumes the proto dependency
                is already included in the target deps. Otherwise it creates a
                new proto_library with name "_{name}_proto" and
                cc_proto_library with name "_{name}_cc_proto" and makes the
                fuzz target depend on the latter.
        proto_deps: Deps for proto. Only used if proto is not empty.
        external_deps: External deps.
        srcs: The source files for the test.
        deps: The dependencies of the test.
        data: The data dependencies of the test.
        size: The size of the test.
        tags: The tags for the test.
        owner: The owning team of the test (for auto-bug-filing).
        end2end_fuzzer: Flag to enable end2end fuzzers.
                        This is currently False and ignored
        **kwargs: Other arguments to supply to the test.
    """

    CORPUS_DIR = native.package_name() + "/" + corpus
    deps = deps + ["@com_google_libprotobuf_mutator//:libprotobuf_mutator"]

    if "gtest" not in external_deps:
        external_deps = external_deps + ["gtest"]

    if proto != None:
        PROTO_LIBRARY = "_%s_proto" % name
        grpc_internal_proto_library(
            name = PROTO_LIBRARY,
            srcs = [proto],
            deps = proto_deps,
        )
        CC_PROTO_LIBRARY = "_%s_cc_proto" % name
        grpc_cc_proto_library(
            name = CC_PROTO_LIBRARY,
            deps = [PROTO_LIBRARY],
        )
        deps = deps + [CC_PROTO_LIBRARY]

    grpc_cc_test(
        name = name,
        srcs = srcs,
        tags = tags + ["grpc-fuzzer", "no-cache"],
        deps = deps + select({
            "//:grpc_build_fuzzers": [],
            "//conditions:default": ["//test/core/test_util:fuzzer_corpus_test"],
        }),
        data = data + native.glob([corpus + "/**"]),
        external_deps = external_deps,
        size = size,
        args = select({
            "//:grpc_build_fuzzers": [CORPUS_DIR, "-runs=20000", "-max_total_time=300"],
            "//conditions:default": ["--directory=" + CORPUS_DIR],
        }),
        **kwargs
    )

def grpc_fuzz_test(name, srcs = [], deps = [], tags = [], external_deps = []):
    """Instantiates a fuzztest based test.

    This is the preferred method of writing fuzzers.

    Args:
        name: The name of the test.
        srcs: The source files for the test.
        deps: The dependencies of the test.
        tags: The tags for the test.
        external_deps: External deps.
    """
    grpc_cc_test(
        name = name,
        srcs = srcs,
        tags = tags + [
            "grpc-fuzzer",
            "grpc-fuzztest",
            "no-cache",
            "no_windows",
            "bazel_only",
        ],
        deps = deps,
        external_deps = external_deps,
    )
