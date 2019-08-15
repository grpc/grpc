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

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])

load("@com_github_grpc_grpc//bazel:objc_grpc_library.bzl", "objc_grpc_library")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")

objc_grpc_library(
    name = "HelloWorld_grpc_proto",
    srcs = ["//examples:protos/helloworld.proto"],
    deps = ["//examples:helloworld_proto"],
    tags = ["manual"],
)

# This one works with import "external/com_github_grpc_grpc/examples/protos/Helloworld.pbrpc.h"
objc_grpc_library(
    name = "HelloWorld_grpc_proto_external",
    srcs = ["//external/com_github_grpc_grpc/examples:protos/helloworld.proto"],
    deps = ["@com_github_grpc_grpc//examples:helloworld_proto"],
    tags = ["manual"],
)

objc_library(
    name = "HelloWorld-lib",
    srcs = glob(["helloworld/**/*.m",]),
    hdrs = glob(["helloworld/**/*.h"]),
    data = glob([
        "helloworld/HelloWorld/Base.lproj/**",
        "helloworld/HelloWorld/Images.xcassets/**",
    ]),
    includes = ["helloworld/HelloWorld"],
    deps = [":HelloWorld_grpc_proto"],
    tags = ["manual"],
)

ios_application(
    name = "HelloWorld",
    bundle_id = "Google.HelloWorld",
    families = [
        "iphone",
        "ipad",
    ],
    minimum_os_version = "8.0",
    infoplists = ["helloworld/HelloWorld/Info.plist"],
    deps = [":HelloWorld-lib"],
    tags = ["manual"],
)
