load("@build_bazel_rules_apple//apple:ios.bzl", "ios_application")
load("@com_github_grpc_grpc//bazel:grpc_objc_library.bzl",  "objc_grpc_library", "objc_proto_grpc_library")

objc_library(
    name = "grpc_objc_client",
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
    name = "grpc_objc_rpc",
    srcs = glob(["ProtoRPC/*.m"]),
    hdrs = glob(["ProtoRPC/*.h"]),
    defines = ["GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0"],
    deps = [
        "//:grpc",
        ":grpc_objc_client",
        "@com_google_protobuf//:protobuf_objc"
    ],
    visibility = ["//visibility:public"]
)

proto_library(
    name = "messages_proto",
    srcs = ["RemoteTest/messages.proto"],
)

objc_proto_grpc_library(
    name = "messages_proto_objc",
    deps = [":messages_proto"]
)

proto_library(
    name = "test_proto",
    srcs = ["RemoteTest/test.proto"],
    deps = [
        ":messages_proto",
        "//examples:helloworld_proto"
    ]
)

objc_grpc_library(
    name = "helloworld_grpc_objc",
    srcs = ["//examples/protos/helloworld.proto"],
    deps = [
        "//examples:helloworld_proto"
    ]
)

objc_grpc_library(
    name = "test_grpc_objc",
    srcs = [
        "RemoteTest/test.proto",
    ],
    use_well_known_protos = True,
    deps = [
        ":test_proto"
    ]
)