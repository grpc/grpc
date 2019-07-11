load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@com_github_grpc_grpc//bazel:grpc_objc_library.bzl",  "objc_grpc_library", "objc_proto_grpc_library")

proto_library(
    name = "messages_proto",
    srcs = ["BazelBuildSamples/messages.proto"]
)

proto_library(
    name = "test_proto",
    srcs = ["BazelBuildSamples/test.proto"],
    deps = [":messages_proto"],
)

objc_grpc_library(
    name = "test_grpc_objc",
    srcs = ["BazelBuildSamples/test.proto"],
    use_well_known_protos = True,
    deps = [":test_proto"]
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