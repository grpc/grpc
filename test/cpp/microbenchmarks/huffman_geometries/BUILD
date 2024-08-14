# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/microbenchmarks/huffman_geometries",
    visibility = "public",
)

genrule(
    name = "generate_huffman_geometries",
    outs = ["shard_%d.cc" % i for i in range(100)] +
           ["shard_%d.h" % i for i in range(100)] + [
        "index.h",
    ],
    cmd = "GEN_OUT=$(@D)/../../../.. $(location //tools/codegen/core:gen_huffman_decompressor) microbenchmarks",
    tools = [
        "//tools/codegen/core:gen_huffman_decompressor",
    ],
)

grpc_cc_library(
    name = "huffman_geometries",
    srcs = ["shard_%d.cc" % i for i in range(100)],
    hdrs = ["shard_%d.h" % i for i in range(100)] + [
        "index.h",
    ],
    deps = ["//:gpr_platform"],
)
