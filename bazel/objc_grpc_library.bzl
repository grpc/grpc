load(
    "//bazel:generate_objc.bzl",
    "generate_objc",
    "generate_objc_hdrs",
    "generate_objc_non_arc_srcs",
    "generate_objc_srcs",
)
load("//bazel:protobuf.bzl", "well_known_proto_libs")

def objc_grpc_library(name, deps, srcs = [], use_well_known_protos = False, **kwargs):
    """Generates messages and/or service stubs for given proto_library and all transitively dependent proto files

    Args:
        name: name of target
        deps: a list of proto_library targets that needs to be compiled
        srcs: a list of labels to proto files with service stubs to be generated,
            labels specified must include service stubs; otherwise Bazel will complain about srcs being empty
        use_well_known_protos: whether to use the well known protos defined in
            @com_google_protobuf//src/google/protobuf, default to false
        **kwargs: other arguments
    """
    objc_grpc_library_name = "_" + name + "_objc_grpc_library"

    generate_objc(
        name = objc_grpc_library_name,
        srcs = srcs,
        deps = deps,
        use_well_known_protos = use_well_known_protos,
        **kwargs
    )

    generate_objc_hdrs(
        name = objc_grpc_library_name + "_hdrs",
        src = ":" + objc_grpc_library_name,
    )

    generate_objc_non_arc_srcs(
        name = objc_grpc_library_name + "_non_arc_srcs",
        src = ":" + objc_grpc_library_name,
    )

    arc_srcs = None
    if len(srcs) > 0:
        generate_objc_srcs(
            name = objc_grpc_library_name + "_srcs",
            src = ":" + objc_grpc_library_name,
        )
        arc_srcs = [":" + objc_grpc_library_name + "_srcs"]

    native.objc_library(
        name = name,
        hdrs = [":" + objc_grpc_library_name + "_hdrs"],
        non_arc_srcs = [":" + objc_grpc_library_name + "_non_arc_srcs"],
        srcs = arc_srcs,
        defines = [
            "GPB_USE_PROTOBUF_FRAMEWORK_IMPORTS=0",
            "GPB_GRPC_FORWARD_DECLARE_MESSAGE_PROTO=0",
        ],
        includes = [
            "_generated_protos",
            "src/objective-c",
        ],
        deps = [
            "@com_github_grpc_grpc//src/objective-c:proto_objc_rpc",
            "@com_google_protobuf//:protobuf_objc",
        ],
        **kwargs
    )
