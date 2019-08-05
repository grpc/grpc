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

package(default_visibility = ["//visibility:private"])

load(
    "//src/objective-c:grpc_objc_internal_library.bzl",
    "grpc_objc_testing_library",
    "testing_objc_grpc_library"
)
load("@build_bazel_rules_apple//apple:resources.bzl", "apple_resource_bundle")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_apple//apple:macos.bzl", "macos_unit_test")
load("@build_bazel_rules_apple//apple:tvos.bzl", "tvos_unit_test")

exports_files(["LICENSE"])

proto_library(
    name = "messages_proto",
    srcs = ["RemoteTestClient/messages.proto"],
)

proto_library(
    name = "test_proto",
    srcs = ["RemoteTestClient/test.proto"],
    deps = [":messages_proto"],
)

testing_objc_grpc_library(
    name = "RemoteTest",
    srcs = ["RemoteTestClient/test.proto"],
    use_well_known_protos = True,
    deps = [":test_proto"],
)

apple_resource_bundle(
    name = "TestCertificates",
    resources = ["TestCertificates.bundle/test-certificates.pem"],
)

# TestConfigs is added to each grpc_objc_testing_library's deps
grpc_objc_testing_library(
    name = "TestConfigs",
    hdrs = ["version.h"],
    data = [":TestCertificates"],
    defines = [
        "HOST_PORT_LOCALSSL=localhost:5051",
        "HOST_PORT_LOCAL=localhost:5050",
        "HOST_PORT_REMOTE=grpc-test.sandbox.googleapis.com",
    ],
)

grpc_objc_testing_library(
    name = "CronetConfig",
    srcs = ["ConfigureCronet.m"],
    hdrs = ["ConfigureCronet.h"],
)

grpc_objc_testing_library(
    name = "InteropTests-lib",
    hdrs = ["InteropTests/InteropTests.h"],
    srcs = ["InteropTests/InteropTests.m"],
    deps = [
        ":InteropTestsBlockCallbacks-lib",
        ":CronetConfig",
    ],
)

grpc_objc_testing_library(
    name = "InteropTestsRemote-lib",
    srcs = ["InteropTests/InteropTestsRemote.m"],
    deps = [":InteropTests-lib"],
)

grpc_objc_testing_library(
    name = "InteropTestsBlockCallbacks-lib",
    hdrs = ["InteropTests/InteropTestsBlockCallbacks.h"],
    srcs = ["InteropTests/InteropTestsBlockCallbacks.m"],
)

grpc_objc_testing_library(
    name = "InteropTestsLocalSSL-lib",
    srcs = ["InteropTests/InteropTestsLocalSSL.m"],
    deps = [":InteropTests-lib"],
)

grpc_objc_testing_library(
    name = "InteropTestsLocalCleartext-lib",
    srcs = ["InteropTests/InteropTestsLocalCleartext.m"],
    deps = [":InteropTests-lib"],
)

grpc_objc_testing_library(
    name = "InteropTestsMultipleChannels-lib",
    srcs = ["InteropTests/InteropTestsMultipleChannels.m"],
    deps = [":InteropTests-lib"],
)

grpc_objc_testing_library(
    name = "RxLibraryUnitTests-lib",
    srcs = ["UnitTests/RxLibraryUnitTests.m"],
)

grpc_objc_testing_library(
    name = "GRPCClientTests-lib",
    srcs = ["UnitTests/GRPCClientTests.m"],
)

grpc_objc_testing_library(
    name = "APIv2Tests-lib",
    srcs = ["UnitTests/APIv2Tests.m"],
)

grpc_objc_testing_library(
    name = "ChannelPoolTest-lib",
    srcs = ["UnitTests/ChannelPoolTest.m"],
)

grpc_objc_testing_library(
    name = "ChannelTests-lib",
    srcs = ["UnitTests/ChannelTests.m"],
)

grpc_objc_testing_library(
    name = "NSErrorUnitTests-lib",
    srcs = ["UnitTests/NSErrorUnitTests.m"],
)

grpc_objc_testing_library(
    name = "MacStressTests-lib",
    srcs = glob([
        "MacTests/*.m",
    ]),
    hdrs = ["MacTests/StressTests.h"],
)

ios_unit_test(
    name = "UnitTests",
    minimum_os_version = "8.0",
    deps = [
        ":RxLibraryUnitTests-lib",
        ":GRPCClientTests-lib",
        ":APIv2Tests-lib",
        ":ChannelPoolTest-lib",
        ":ChannelTests-lib",
        ":NSErrorUnitTests-lib",
    ]
)

ios_unit_test(
    name = "InteropTests",
    minimum_os_version = "8.0",
    deps = [
        ":InteropTestsRemote-lib",
        ":InteropTestsLocalSSL-lib",
        ":InteropTestsLocalCleartext-lib",
        # ":InteropTestsMultipleChannels-lib", #??????? Cronet must be used?
    ],
)

macos_unit_test(
    name = "MacTests",
    minimum_os_version = "10.9",
    deps = [
        ":APIv2Tests-lib",
        ":RxLibraryUnitTests-lib",
        ":NSErrorUnitTests-lib",
        ":InteropTestsRemote-lib",
        ":InteropTestsLocalSSL-lib",
        ":InteropTestsLocalCleartext-lib",
        ":MacStressTests-lib",
    ]
)

# cares does not support tvOS CPU architecture with Bazel yet
tvos_unit_test(
    name = "TvTests",
    minimum_os_version = "10.0",
    deps = [
        ":APIv2Tests-lib",
        ":RxLibraryUnitTests-lib",
        ":NSErrorUnitTests-lib",
        ":InteropTestsRemote-lib",
        ":InteropTestsLocalSSL-lib",
        ":InteropTestsLocalCleartext-lib",
    ]
)