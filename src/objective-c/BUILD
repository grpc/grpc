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

load("//bazel:grpc_build_system.bzl", "grpc_objc_library", "grpc_generate_objc_one_off_targets")

exports_files(["LICENSE"])

grpc_generate_objc_one_off_targets()

grpc_objc_library(
    name = "rx_library_headers",
    hdrs = glob([
        "RxLibrary/*.h",
    ]),
    includes = ["."],
)

grpc_objc_library(
    name = "rx_library",
    srcs = glob([
        "RxLibrary/*.m",
    ]),
    includes = ["."],
    deps = [
        ":rx_library_headers",
        ":rx_library_private",
    ],
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
    name = "grpc_objc_interface_legacy",
    hdrs = [
        "GRPCClient/GRPCCall+ChannelArg.h",
        "GRPCClient/GRPCCall+ChannelCredentials.h",
        "GRPCClient/GRPCCall+Cronet.h",
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
        "GRPCClient/GRPCCallLegacy.h",
        "GRPCClient/GRPCTypes.h",
    ],
    deps = [
        "rx_library_headers",
    ],
)

grpc_objc_library(
    name = "grpc_objc_interface",
    hdrs = [
        "GRPCClient/GRPCCall.h",
        "GRPCClient/GRPCCall+Interceptor.h",
        "GRPCClient/GRPCCallOptions.h",
        "GRPCClient/GRPCInterceptor.h",
        "GRPCClient/GRPCTransport.h",
        "GRPCClient/GRPCDispatchable.h",
        "GRPCClient/internal/GRPCCallOptions+Internal.h",
        "GRPCClient/version.h",
    ],
    srcs = [
        "GRPCClient/GRPCCall.m",
        "GRPCClient/GRPCCall+Interceptor.m",
        "GRPCClient/GRPCCallOptions.m",
        "GRPCClient/GRPCInterceptor.m",
        "GRPCClient/GRPCTransport.m",
        "GRPCClient/private/GRPCTransport+Private.m",
    ],
    includes = ["."],
    textual_hdrs = [
        "GRPCClient/private/GRPCTransport+Private.h",
    ],
    deps = [
        ":grpc_objc_interface_legacy",
    ],
)

grpc_objc_library(
    name = "grpc_objc_client_core",
    hdrs = [
        "GRPCClient/GRPCCall+ChannelCredentials.h",
        "GRPCClient/GRPCCall+Cronet.h",
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
        "GRPCClient/GRPCCall+ChannelArg.h",
    ],
    textual_hdrs = glob(["GRPCClient/private/GRPCCore/*.h"]),
    srcs = [
        "GRPCClient/GRPCCall+ChannelArg.m",
        "GRPCClient/GRPCCall+ChannelCredentials.m",
        "GRPCClient/GRPCCall+Cronet.m",
        "GRPCClient/GRPCCall+OAuth2.m",
        "GRPCClient/GRPCCall+Tests.m",
        "GRPCClient/GRPCCallLegacy.m",
    ] + glob(["GRPCClient/private/GRPCCore/*.m"]),
    data = [":gRPCCertificates"],
    includes = ["."],
    deps = [
        ":grpc_objc_interface",
        ":grpc_objc_interface_legacy",
        ":rx_library",
        "//:grpc_objc",
    ],
)

alias(
    name = "grpc_objc_client",
    actual = "grpc_objc_client_core",
)

grpc_objc_library(
    name = "proto_objc_rpc_legacy_header",
    hdrs = [
        "ProtoRPC/ProtoRPCLegacy.h",
    ],
    includes = ["."],
)

grpc_objc_library(
    name = "proto_objc_rpc_v2",
    srcs = [
        "ProtoRPC/ProtoMethod.m",
        "ProtoRPC/ProtoRPC.m",
        "ProtoRPC/ProtoService.m",
    ],
    hdrs = [
        "ProtoRPC/ProtoMethod.h",
        "ProtoRPC/ProtoRPC.h",
        "ProtoRPC/ProtoService.h",
    ],
    includes = ["."],
    defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
    deps = [
        ":grpc_objc_interface",
        ":proto_objc_rpc_legacy_header",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

grpc_objc_library(
    name = "proto_objc_rpc",
    srcs = [
        "ProtoRPC/ProtoRPCLegacy.m",
        "ProtoRPC/ProtoServiceLegacy.m",
    ],
    hdrs = [
        "ProtoRPC/ProtoMethod.h",
        "ProtoRPC/ProtoRPCLegacy.h",
        "ProtoRPC/ProtoService.h",
    ],
    deps = [
        ":rx_library",
        ":proto_objc_rpc_v2",
        ":proto_objc_rpc_legacy_header",
        ":grpc_objc_client_core",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

load("@build_bazel_rules_apple//apple:resources.bzl", "apple_resource_bundle")

apple_resource_bundle(
    # The choice of name is signicant here, since it determines the bundle name.
    name = "gRPCCertificates",
    resources = ["//:etc/roots.pem"],
)

# Internal target combining grpc_objc_client_core and proto_objc_rpc for testing
grpc_objc_library(
    name = "grpc_objc_client_core_internal_testing",
    hdrs = [
        "GRPCClient/GRPCCall+ChannelCredentials.h",
        "GRPCClient/GRPCCall+Cronet.h",
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
        "GRPCClient/GRPCCall+ChannelArg.h",
        "GRPCClient/internal_testing/GRPCCall+InternalTests.h",
    ],
    textual_hdrs = glob(["GRPCClient/private/GRPCCore/*.h"]),
    srcs = [
        "GRPCClient/GRPCCall+ChannelArg.m",
        "GRPCClient/GRPCCall+ChannelCredentials.m",
        "GRPCClient/GRPCCall+Cronet.m",
        "GRPCClient/GRPCCall+OAuth2.m",
        "GRPCClient/GRPCCall+Tests.m",
        "GRPCClient/GRPCCallLegacy.m",
        "GRPCClient/internal_testing/GRPCCall+InternalTests.m",
    ] + glob(["GRPCClient/private/GRPCCore/*.m"]),
    data = [":gRPCCertificates"],
    includes = ["."],
    defines = [
        "GRPC_TEST_OBJC=1",
    ],
    deps = [
        ":grpc_objc_interface",
        ":grpc_objc_interface_legacy",
        ":rx_library",
        "//:grpc_objc",
    ],
)

grpc_objc_library(
    name = "proto_objc_rpc_internal_testing",
    srcs = [
        "ProtoRPC/ProtoRPCLegacy.m",
        "ProtoRPC/ProtoServiceLegacy.m",
    ],
    hdrs = [
        "ProtoRPC/ProtoMethod.h",
        "ProtoRPC/ProtoRPC.h",
        "ProtoRPC/ProtoRPCLegacy.h",
        "ProtoRPC/ProtoService.h",
    ],
    deps = [
        ":rx_library",
        ":proto_objc_rpc_v2",
        ":proto_objc_rpc_legacy_header",
        ":grpc_objc_client_core_internal_testing",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

alias(
    name = "grpc_objc_client_internal_testing",
    actual = "proto_objc_rpc_internal_testing",
)
