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

load("@com_github_grpc_grpc//bazel:objc_grpc_library.bzl", "objc_grpc_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_apple//apple:macos.bzl", "macos_application")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

objc_grpc_library(
    name = "HelloWorld_grpc_proto",
    srcs = ["//examples/protos:helloworld.proto"],
    tags = ["manual"],
    deps = ["//examples/protos:helloworld_proto"],
)

# This one works with import "external/com_github_grpc_grpc/examples/protos/Keyvaluestore.pbrpc.h"
objc_grpc_library(
    name = "Keyvaluestore_grpc_proto_external",
    srcs = ["//external/com_github_grpc_grpc/examples/protos:keyvaluestore.proto"],
    tags = ["manual"],
    deps = ["@com_github_grpc_grpc//examples/protos:keyvaluestore_proto"],
)

objc_library(
    name = "HelloWorld-lib",
    srcs = glob(["helloworld/**/*.m"]),
    hdrs = glob(["helloworld/**/*.h"]),
    data = glob([
        "helloworld/HelloWorld/Base.lproj/**",
        "helloworld/HelloWorld/Images.xcassets/**",
    ]),
    includes = ["helloworld/HelloWorld"],
    tags = ["manual"],
    deps = [":HelloWorld_grpc_proto"],
)

ios_application(
    name = "HelloWorld",
    bundle_id = "Google.HelloWorld",
    families = [
        "iphone",
        "ipad",
    ],
    infoplists = ["helloworld/HelloWorld/Info.plist"],
    minimum_os_version = "9.0",
    tags = ["manual"],
    deps = [":HelloWorld-lib"],
)

objc_library(
    name = "HelloWorldMacos-lib",
    srcs = glob(["helloworld_macos/**/*.m"]),
    hdrs = glob(["helloworld_macos/**/*.h"]),
    data = glob([
        "helloworld_macos/HelloWorld/Base.lproj/**",
        "helloworld_macos/HelloWorld/Images.xcassets/**",
    ]),
    includes = ["helloworld_macos/HelloWorld"],
    tags = ["manual"],
    deps = [":HelloWorld_grpc_proto"],
)

macos_application(
    name = "HelloWorldMacos",
    bundle_id = "io.grpc.HelloWorld",
    entitlements = "helloworld_macos/HelloWorld/Helloworld.entitlements",
    infoplists = ["helloworld_macos/HelloWorld/Info.plist"],
    minimum_os_version = "10.13",
    tags = ["manual"],
    deps = [":HelloWorldMacos-lib"],
)

objc_grpc_library(
    name = "RouteGuide",
    srcs = ["//examples/protos:route_guide.proto"],
    tags = ["manual"],
    deps = ["//examples/protos:route_guide_proto"],
)

objc_library(
    name = "RouteGuideClient-lib",
    srcs = glob(["route_guide/**/*.m"]),
    hdrs = glob(["route_guide/**/*.h"]),
    data = glob([
        "route_guide/Misc/Base.lproj/**",
        "route_guide/Misc/Images.xcassets/**",
    ]) + ["route_guide/route_guide_db.json"],
    includes = ["route_guide/Misc"],
    tags = ["manual"],
    deps = [":RouteGuide"],
)

ios_application(
    name = "RouteGuideClient",
    bundle_id = "gRPC.RouteGuideClient",
    families = [
        "iphone",
        "ipad",
    ],
    infoplists = ["route_guide/Misc/Info.plist"],
    minimum_os_version = "9.0",
    tags = ["manual"],
    deps = [":RouteGuideClient-lib"],
)
