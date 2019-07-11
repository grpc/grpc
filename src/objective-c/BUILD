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
