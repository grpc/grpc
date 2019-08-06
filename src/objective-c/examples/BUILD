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


load("//src/objective-c:grpc_objc_internal_library.bzl", "local_objc_grpc_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_apple//apple:tvos.bzl", "tvos_application")
load("@build_bazel_rules_apple//apple:watchos.bzl", "watchos_application")

proto_library(
    name = "messages_proto",
    srcs = ["RemoteTestClient/messages.proto"],
)

proto_library(
    name = "test_proto",
    srcs = ["RemoteTestClient/test.proto"],
    deps = [":messages_proto"],
)

# use objc_grpc_library in bazel:objc_grpc_library.bzl when developing outside the repo
local_objc_grpc_library(
    name = "test_grpc_objc",
    srcs = ["RemoteTestClient/test.proto"],
    use_well_known_protos = True,
    deps = [
        "//src/objective-c/examples:test_proto",
    ],
)

# Proof that without this works without srcs
local_objc_grpc_library(
    name = "test_objc",
    use_well_known_protos = True,
    deps = [
        "//src/objective-c/examples:test_proto",
    ],
)

objc_library(
    name = "Sample-lib",
    srcs = glob(["Sample/**/*.m"]),
    hdrs = glob(["Sample/**/*.h"]),
    data = glob([
        "Sample/Sample/Base.lproj/**",
        "Sample/Sample/Images.xcassets/**",
    ]),
    deps = [":test_grpc_objc"],
)

ios_application(
    name = "Sample",
    bundle_id = "grpc.objc.examples.Sample",
    minimum_os_version = "8.0",
    infoplists = ["Sample/Sample/Info.plist"],
    families = [
        "iphone",
        "ipad",
    ],
    deps = ["Sample-lib"],
)

objc_library(
    name = "InterceptorSample-lib",
    srcs = glob(["InterceptorSample/**/*.m"]),
    hdrs = glob(["InterceptorSample/**/*.h"]),
    data = glob([
        "InterceptorSample/InterceptorSample/Base.lproj/**",
        "InterceptorSample/InterceptorSample/Images.xcassets/**",
    ]),
    deps = [":test_grpc_objc"],
)

ios_application(
    name = "InterceptorSample",
    bundle_id = "grpc.objc.examples.InterceptorSample",
    minimum_os_version = "9.0", # Safe Area Layout Guide used
    infoplists = ["InterceptorSample/InterceptorSample/Info.plist"],
    families = [
        "iphone",
        "ipad",
    ],
    deps = ["InterceptorSample-lib"],
)
