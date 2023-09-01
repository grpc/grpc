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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package", "grpc_proto_library")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_package(
    name = "test/core/transport/binder/end2end/fuzzers",
    features = [
        "layering_check",
    ],
)

licenses(["notice"])

# Protobuf messages for generating inputs. We manually define proto
# library rule here so the same proto file can be shared between multiple
# grpc_proto_fuzzer targets
grpc_proto_library(
    name = "binder_transport_fuzzer_proto",
    srcs = ["binder_transport_fuzzer.proto"],
)

grpc_cc_library(
    name = "fuzzer_utils",
    srcs = ["fuzzer_utils.cc"],
    language = "c++",
    public_hdrs = ["fuzzer_utils.h"],
    deps = [
        "binder_transport_fuzzer_proto",
        "//:gpr",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_fuzzer(
    name = "binder_transport_client_fuzzer",
    srcs = [
        "client_fuzzer.cc",
    ],
    corpus = "binder_transport_client_fuzzer_corpus",
    owner = "binder",
    proto = "client.proto",
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "binder_transport_fuzzer_proto",
        ":fuzzer_utils",
        "//:gpr",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_fuzzer(
    name = "binder_transport_server_fuzzer",
    srcs = [
        "server_fuzzer.cc",
    ],
    corpus = "binder_transport_server_fuzzer_corpus",
    owner = "binder",
    proto = "server.proto",
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "binder_transport_fuzzer_proto",
        ":fuzzer_utils",
        "//:gpr",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/core:slice",
        "//test/core/util:grpc_test_util",
    ],
)
