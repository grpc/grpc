# gRPC Bazel BUILD file.
#
# Copyright 2019 gRPC authors.
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

licenses(["notice"])  # Apache v2

package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_build_system.bzl", "grpc_objc_library", "grpc_objc_use_cronet_config")

exports_files(["LICENSE"])

grpc_objc_use_cronet_config()

grpc_objc_library(
    name = "rx_library",
    srcs = glob([
        "RxLibrary/*.m",
        "RxLibrary/transformations/*.m",
    ]),
    hdrs = glob([
        "RxLibrary/*.h",
        "RxLibrary/transformations/*.h",
    ]),
    includes = ["."],
    deps = [":rx_library_private"],
)

grpc_objc_library(
    name = "rx_library_private",
    srcs = glob([
        "RxLibrary/private/*.m",
    ]),
    textual_hdrs = glob([
        "RxLibrary/private/*.h",
    ]),
    visibility = ["//visibility:private"],
)

grpc_objc_library(
    name = "grpc_objc_client",
    srcs = glob(
        [
            "GRPCClient/*.m",
            "GRPCClient/private/*.m",
        ],
        exclude = ["GRPCClient/GRPCCall+GID.m"],
    ),
    hdrs = glob(
        [
            "GRPCClient/*.h",
            "GRPCClient/internal/*.h",
        ],
        exclude = ["GRPCClient/GRPCCall+GID.h"],
    ),
    data = ["//:gRPCCertificates"],
    includes = ["."],
    textual_hdrs = glob([
        "GRPCClient/private/*.h",
    ]),
    deps = [
        ":rx_library",
        "//:grpc_objc",
    ],
)

grpc_objc_library(
    name = "proto_objc_rpc",
    srcs = glob(
        ["ProtoRPC/*.m"],
    ),
    hdrs = glob(
        ["ProtoRPC/*.h"],
    ),
    # Different from Cocoapods, do not import as if @com_google_protobuf//:protobuf_objc is a framework,
    # use the real paths of @com_google_protobuf//:protobuf_objc instead
    defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
    deps = [
        ":grpc_objc_client",
        ":rx_library",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

grpc_objc_library(
    name = "grpc_objc_client_internal_testing",
    srcs = glob(
        [
            "GRPCClient/*.m",
            "GRPCClient/private/*.m",
            "GRPCClient/internal_testing/*.m",
            "ProtoRPC/*.m",
        ],
        exclude = ["GRPCClient/GRPCCall+GID.m"],
    ),
    hdrs = glob(
        [
            "GRPCClient/*.h",
            "GRPCClient/internal/*.h",
            "GRPCClient/internal_testing/*.h",
            "ProtoRPC/*.h",
        ],
        exclude = ["GRPCClient/GRPCCall+GID.h"],
    ),
    includes = ["."],
    data = ["//:gRPCCertificates"],
    defines = [
        "GRPC_TEST_OBJC=1",
        "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
    ],
    textual_hdrs = glob(
        [
            "GRPCClient/private/*.h",
        ],
    ),
    deps = [
        ":rx_library",
        "//:grpc_objc",
        "@com_google_protobuf//:protobuf_objc",
    ],
)
