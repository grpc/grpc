load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load(
    "@com_github_grpc_grpc//bazel:grpc_objc_library.bzl", 
    "objc_proto_lib", 
    "objc_grpc_lib",
)

objc_library(
    name = "grpc_objc",
    srcs = glob([
            "GRPCClient/**/*.m", 
            "RxLibrary/**/*.m"
        ],
        exclude = ["GRPCClient/GRPCCall+GID.m"]
    ),
    hdrs = glob([
            "GRPCClient/**/*.h", 
            "RxLibrary/**/*.h"
        ], 
        exclude = ["GRPCClient/GRPCCall+GID.h"]
    ),
    includes = ["."],
    sdk_frameworks = ["SystemConfiguration"],
    deps = [
        "//:grpc",
        "//:gRPCCertificates"
    ],
    visibility = ["//visibility:public"]
)

objc_library(
    name = "grpc_objc_protoRPC",
    srcs = glob(["ProtoRPC/*.m"]),
    hdrs = glob(["ProtoRPC/*.h"]),
    defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
    deps = [
        "//:grpc",
        ":grpc_objc",
        "@com_google_protobuf//:protobuf_objc"
    ],
    visibility = ["//visibility:public"]
)

proto_library(
    name = "messages_proto",
    srcs = ["RemoteTest/messages.proto"],
)

proto_library(
    name = "test_proto",
    srcs = ["RemoteTest/test.proto"],
    deps = [
        ":messages_proto",
    ]
)

objc_proto_lib(
    name = "messages_proto_objc",
    srcs = [":messages_proto"],
)

objc_proto_lib(
    name = "test_proto_objc",
    srcs = [":test_proto"],
    use_well_known_protos = True,
    deps = [
        ":messages_proto_objc"
    ]
)

objc_grpc_lib(
    name = "test_grpc_objc",
    srcs = [":test_proto"],
    use_well_known_protos = True,
    deps = [
        ":messages_proto_objc",
        ":test_proto_objc"
    ]
)

objc_library(
    name = "InterceptorSampleLib",
    srcs = glob(["examples/InterceptorSample/InterceptorSample/**/*.m"]),
    hdrs = glob(["examples/InterceptorSample/InterceptorSample/**/*.h"]),
    data = glob([
        "examples/InterceptorSample/InterceptorSample/Assets.xcassets/**/*",
        "examples/InterceptorSample/InterceptorSample/Base.lproj/**/*"
    ]),
    deps = [
        ":messages_proto_objc",
        ":test_proto_objc",
        ":test_grpc_objc",
    ]
)

ios_application(
    name = "InterceptorSample",
    bundle_id = "gRPC.InterceptorSample2",
    families = ["iphone"],
    minimum_os_version = "9.0",
    infoplists = ["examples/InterceptorSample/InterceptorSample/Info.plist"],
    visibility = ["//visibility:public"],
    deps = [":InterceptorSampleLib"],
)