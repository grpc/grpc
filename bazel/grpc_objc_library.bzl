load(
    "//bazel:generate_objc.bzl",
    "generate_objc",
    "generate_objc_hdrs",
    "generate_objc_srcs",
)
load("//bazel:protobuf.bzl", "well_known_proto_libs")

def objc_proto_lib(name, srcs, deps = [], use_well_known_protos = False, **kwargs):
    objc_proto_library_name = "_" + name + "_objc_proto_library"

    generate_objc(
        name = objc_proto_library_name,
        srcs = srcs,
        proto_only = True,
        use_well_known_protos = use_well_known_protos,
        **kwargs
    )

    generate_objc_hdrs(
        name = objc_proto_library_name + "_hdrs",
        src = ":" + objc_proto_library_name
    )
    generate_objc_srcs(
        name = objc_proto_library_name + "_srcs",
        src = ":" + objc_proto_library_name
    )

    native.objc_library(
        name = name,
        hdrs = [":" + objc_proto_library_name + "_hdrs"],
        non_arc_srcs = [":" + objc_proto_library_name + "_srcs"],
        defines = [
            "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
            "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=0"
        ],
        includes = ["."],
        deps = deps + [
            "@com_google_protobuf//:protobuf_objc"
        ]
    )

def objc_grpc_lib(name, srcs, deps = [], use_well_known_protos = False, **kwargs):
    objc_grpc_library_name = "_" + name + "_objc_proto_library"

    generate_objc(
        name = objc_grpc_library_name,
        srcs = srcs,
        proto_only = False,
        use_well_known_protos = use_well_known_protos,
        **kwargs
    )

    generate_objc_hdrs(
        name = objc_grpc_library_name + "_hdrs",
        src = ":" + objc_grpc_library_name
    )
    generate_objc_srcs(
        name = objc_grpc_library_name + "_srcs",
        src = ":" + objc_grpc_library_name
    )

    native.objc_library(
        name = name,
        hdrs = [":" + objc_grpc_library_name + "_hdrs"],
        srcs = [":" + objc_grpc_library_name + "_srcs"],
        defines = [
            "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
            "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=0"
        ],
        includes = ["."],
        deps = deps + [
            "@com_github_grpc_grpc//src/objective-c:grpc_objc_protoRPC",
            "@com_google_protobuf//:protobuf_objc"
        ]
    )
