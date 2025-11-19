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

load("@build_bazel_rules_apple//apple:resources.bzl", "apple_resource_bundle")
load("//bazel:grpc_build_system.bzl", "grpc_generate_objc_one_off_targets", "grpc_objc_library")

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
)

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
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
        "GRPCClient/GRPCCallLegacy.h",
    ],
    deps = [
        ":grpc_objc_interface_types",
        ":rx_library_headers",
    ],
)

grpc_objc_library(
    name = "grpc_objc_interface_types",
    srcs = [
        "GRPCClient/GRPCTypes.mm",
    ],
    hdrs = [
        "GRPCClient/GRPCTypes.h",
    ],
)

grpc_objc_library(
    name = "grpc_objc_interface",
    srcs = [
        "GRPCClient/GRPCCall.mm",
        "GRPCClient/GRPCCall+Interceptor.mm",
        "GRPCClient/GRPCCallOptions.mm",
        "GRPCClient/GRPCInterceptor.mm",
        "GRPCClient/GRPCTransport.mm",
        "GRPCClient/private/GRPCTransport+Private.mm",
    ],
    hdrs = [
        "GRPCClient/GRPCCall.h",
        "GRPCClient/GRPCCall+Interceptor.h",
        "GRPCClient/GRPCCallOptions.h",
        "GRPCClient/GRPCDispatchable.h",
        "GRPCClient/GRPCInterceptor.h",
        "GRPCClient/GRPCTransport.h",
        "GRPCClient/internal/GRPCCallOptions+Internal.h",
        "GRPCClient/version.h",
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
    name = "grpc_objc_client",
    srcs = [
        "GRPCClient/GRPCCall+ChannelArg.mm",
        "GRPCClient/GRPCCall+ChannelCredentials.mm",
        "GRPCClient/GRPCCall+OAuth2.mm",
        "GRPCClient/GRPCCall+Tests.mm",
        "GRPCClient/GRPCCallLegacy.mm",
    ] + glob(["GRPCClient/private/GRPCCore/*.mm"]),
    hdrs = [
        "GRPCClient/GRPCCall+ChannelArg.h",
        "GRPCClient/GRPCCall+ChannelCredentials.h",
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
    ],
    data = [":gRPCCertificates"],
    includes = ["."],
    textual_hdrs = glob(["GRPCClient/private/GRPCCore/*.h"]),
    deps = [
        ":grpc_objc_interface",
        ":grpc_objc_interface_legacy",
        ":rx_library",
        ":rx_library_headers",
        "//:gpr",
        "//:grpc",
        "//:grpc++_public_hdrs",
        "//:grpc_core_credentials_header",
        "//:grpc_objc",
    ],
)

# TODO (mxyan): Switch "name" and "actual" when import is done
# Some internal Swift projects will need to be updated with the new name
alias(
    name = "grpc_objc_client_core",
    actual = "grpc_objc_client",
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
    defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
    includes = ["."],
    deps = [
        ":grpc_objc_interface",
        ":proto_objc_rpc_legacy_header",
        ":rx_library_headers",
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
        "ProtoRPC/ProtoRPC.h",
        "ProtoRPC/ProtoRPCLegacy.h",
        "ProtoRPC/ProtoService.h",
    ],
    deps = [
        ":grpc_objc_client_core",
        ":proto_objc_rpc_legacy_header",
        ":proto_objc_rpc_v2",
        ":rx_library",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

apple_resource_bundle(
    # The choice of name is signicant here, since it determines the bundle name.
    name = "gRPCCertificates",
    resources = ["//:etc/roots.pem"],
)

# Internal target combining grpc_objc_client_core and proto_objc_rpc for testing
grpc_objc_library(
    name = "grpc_objc_client_core_internal_testing",
    srcs = [
        "GRPCClient/GRPCCall+ChannelArg.mm",
        "GRPCClient/GRPCCall+ChannelCredentials.mm",
        "GRPCClient/GRPCCall+OAuth2.mm",
        "GRPCClient/GRPCCall+Tests.mm",
        "GRPCClient/GRPCCallLegacy.mm",
        "GRPCClient/internal_testing/GRPCCall+InternalTests.mm",
    ] + glob(["GRPCClient/private/GRPCCore/*.mm"]),
    hdrs = [
        "GRPCClient/GRPCCall+ChannelArg.h",
        "GRPCClient/GRPCCall+ChannelCredentials.h",
        "GRPCClient/GRPCCall+OAuth2.h",
        "GRPCClient/GRPCCall+Tests.h",
        "GRPCClient/internal_testing/GRPCCall+InternalTests.h",
    ],
    data = [":gRPCCertificates"],
    defines = [
        "GRPC_TEST_OBJC=1",
    ],
    includes = ["."],
    textual_hdrs = glob(["GRPCClient/private/GRPCCore/*.h"]),
    deps = [
        ":grpc_objc_interface",
        ":grpc_objc_interface_legacy",
        ":rx_library",
        ":rx_library_headers",
        "//:gpr",
        "//:grpc",
        "//:grpc++_public_hdrs",
        "//:grpc_core_credentials_header",
        "//:grpc_objc",
        "//:grpc_security_base",
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
        ":grpc_objc_client_core_internal_testing",
        ":grpc_objc_interface",
        ":grpc_objc_interface_legacy",
        ":proto_objc_rpc_legacy_header",
        ":proto_objc_rpc_v2",
        ":rx_library",
        ":rx_library_headers",
        "//:gpr",
        "//:grpc",
        "//:grpc++_public_hdrs",
        "//:grpc_core_credentials_header",
        "@com_google_protobuf//:protobuf_objc",
    ],
)

alias(
    name = "grpc_objc_client_internal_testing",
    actual = "proto_objc_rpc_internal_testing",
)
