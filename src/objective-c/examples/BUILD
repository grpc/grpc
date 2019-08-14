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

licenses(["notice"])  # 3-clause BSD

package(default_visibility = ["//visibility:public"])

load(
    "//src/objective-c:grpc_objc_internal_library.bzl", 
    "grpc_objc_examples_library",
    "local_objc_grpc_library",
    "proto_library_objc_wrapper",
)
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@build_bazel_rules_apple//apple:tvos.bzl", "tvos_application")
load("@build_bazel_rules_apple//apple:watchos.bzl", "watchos_application", "watchos_extension")

proto_library_objc_wrapper(
    name = "messages_proto",
    srcs = ["RemoteTestClient/messages.proto"],
)

proto_library_objc_wrapper(
    name = "test_proto",
    srcs = ["RemoteTestClient/test.proto"],
    deps = [":messages_proto"],
    use_well_known_protos = True,
)

# use objc_grpc_library in bazel:objc_grpc_library.bzl when developing outside the repo
local_objc_grpc_library(
    name = "RemoteTest",
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

grpc_objc_examples_library(
    name = "Sample-lib",
    srcs = glob(["Sample/Sample/**/*.m"]),
    hdrs = glob(["Sample/Sample/**/*.h"]),
    data = glob([
        "Sample/Sample/Base.lproj/**",
        "Sample/Sample/Images.xcassets/**",
    ]),
)

ios_application(
    name = "Sample",
    bundle_id = "io.grpc.Sample",
    minimum_os_version = "8.0",
    infoplists = ["Sample/Sample/Info.plist"],
    families = [
        "iphone",
        "ipad",
    ],
    deps = ["Sample-lib"],
    visibility = ["//visibility:public"],
)

grpc_objc_examples_library(
    name = "InterceptorSample-lib",
    srcs = glob(["InterceptorSample/InterceptorSample/**/*.m"]),
    hdrs = glob(["InterceptorSample/InterceptorSample/**/*.h"]),
    data = glob([
        "InterceptorSample/InterceptorSample/Base.lproj/**",
        "InterceptorSample/InterceptorSample/Images.xcassets/**",
    ]),
)

ios_application(
    name = "InterceptorSample",
    bundle_id = "io.grpc.InterceptorSample",
    minimum_os_version = "9.0", # Safe Area Layout Guide used
    infoplists = ["InterceptorSample/InterceptorSample/Info.plist"],
    families = [
        "iphone",
        "ipad",
    ],
    deps = ["InterceptorSample-lib"],
)

grpc_objc_examples_library(
    name = "tvOS-sample-lib",
    srcs = glob(["tvOS-sample/tvOS-sample/**/*.m"]),
    hdrs = glob(["tvOS-sample/tvOS-sample/**/*.h"]),
    data = glob([
        "tvOS-sample/tvOS-sample/Base.lproj/**",
        "tvOS-sample/tvOS-sample/Images.xcassets/**",
    ]),
)

# c-ares does not support tvOS CPU architecture with Bazel yet
tvos_application(
    name = "tvOS-sample",
    bundle_id = "io.grpc.tvOS-sample",
    minimum_os_version = "10.0",
    infoplists = ["tvOS-sample/tvOS-sample/Info.plist"],
    deps = [":tvOS-sample-lib"],
)

grpc_objc_examples_library(
    name = "watchOS-sample-iOS-lib",
    srcs = glob(["watchOS-sample/watchOS-sample/**/*.m"]),
    hdrs = glob(["watchOS-sample/watchOS-sample/**/*.h"]),
    data = glob([
        "watchOS-sample/watchOS-sample/Base.lproj/**",
        "watchOS-sample/watchOS-sample/Images.xcassets/**",
    ]),
)

grpc_objc_examples_library(
    name = "watchOS-sample-extension-lib",
    srcs = glob(["watchOS-sample/WatchKit-Extention/**/*.m"]),
    hdrs = glob(["watchOS-sample/WatchKit-Extension/**/*.h"]),
    sdk_frameworks = [
        "WatchConnectivity",
        "WatchKit",
    ],
)

ios_application(
    name = "watchOS-sample",
    bundle_id = "io.grpc.watchOS-sample",
    minimum_os_version = "9.0", # Safe Area Layout Guide used
    families = ["iphone"],
    infoplists = ["watchOS-sample/watchOS-sample/Info.plist"],
    deps = [":watchOS-sample-iOS-lib"],
    watch_application = "watchOS-sample-watchApp",
)

# c-ares does not support watchOS CPU architecture with Bazel yet
watchos_application(
    name = "watchOS-sample-watchApp",
    bundle_id = "io.grpc.watchOS-sample.watchkitapp",
    minimum_os_version = "4.0",
    storyboards = ["watchOS-sample/WatchKit-App/Base.lproj/Interface.storyboard"],
    infoplists = ["watchOS-sample/WatchKit-App/Info.plist"],
    extension = ":watchOS-sample-extension",
)

watchos_extension(
    name = "watchOS-sample-extension",
    bundle_id = "io.grpc.watchOS-sample.watchkitapp.watchkitextension",
    minimum_os_version = "4.0",
    infoplists = ["watchOS-sample/WatchKit-Extension/Info.plist"],
    deps = [":watchOS-sample-extension-lib"],
)
