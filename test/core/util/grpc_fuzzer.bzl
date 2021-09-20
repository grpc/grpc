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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

def grpc_fuzzer(name, corpus, srcs = [], deps = [], data = [], size = "large", **kwargs):
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
            "//:grpc_build_fuzzers": [native.package_name() + "/" + corpus],
            "//conditions:default": ["--directory=" + native.package_name() + "/" + corpus],
        }),
        **kwargs
    )

def grpc_proto_fuzzer(name, corpus, proto, srcs = [], deps = [], data = [], size = "large", **kwargs):
    proto_library(
        name = name + "-proto",
        srcs = [proto],
    )

    cc_proto_library(
        name = name + "-cc_proto",
        deps = [name + "-proto"],
    )

    grpc_cc_test(
        name = name,
        srcs = srcs,
        deps = deps + [
            "@com_google_libprotobuf-mutator//:libprotobuf-mutator",
            name + "-cc_proto",
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
            "//:grpc_build_fuzzers": [native.package_name() + "/" + corpus],
            "//conditions:default": ["--directory=" + native.package_name() + "/" + corpus],
        }),
        **kwargs
    )
