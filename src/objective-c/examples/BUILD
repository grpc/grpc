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


load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load(
    "@com_github_grpc_grpc//bazel:objc_grpc_library.bzl",
    "objc_grpc_library",
)

proto_library(
    name = "messages_proto",
    srcs = ["BazelBuildSamples/messages.proto"],
    visibility = ["//visibility:public"],
)

objc_grpc_library(
    name = "test_grpc_objc",
    srcs = ["BazelBuildSamples/rmt/test.proto"],
    use_well_known_protos = True,
    deps = [
        "//src/objective-c/examples/BazelBuildSamples/rmt:test_proto",
    ],
)

# Proof that without this works without srcs
objc_grpc_library(
    name = "test_objc",
    use_well_known_protos = True,
    deps = [
        "//src/objective-c/examples/BazelBuildSamples/rmt:test_proto",
    ]
)

objc_library(
    name = "ios-sample-lib",
    srcs = glob(["BazelBuildSamples/ios-sample/ios-sample/**/*.m"]),
    hdrs = glob(["BazelBuildSamples/ios-sample/ios-sample/**/*.h"]),
    data = glob([
        "BazelBuildSamples/ios-sample/ios-sample/Assets.xcassets/**/*",
        "BazelBuildSamples/ios-sample/ios-sample/Base.lproj/**/*"
    ]),
    deps = [
        ":test_grpc_objc",
    ]
)

ios_application(
    name = "ios-sample",
    bundle_id = "com.google.ios-sample-objc-bazel",
    families = ["iphone"],
    minimum_os_version = "9.0",
    infoplists = ["BazelBuildSamples/ios-sample/ios-sample/Info.plist"],
    visibility = ["//visibility:public"],
    deps = [":ios-sample-lib"],
)