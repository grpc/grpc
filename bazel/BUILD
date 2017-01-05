package(default_visibility = ["//:__subpackages__"])

load(":cc_grpc_library.bzl", "cc_grpc_library")

cc_grpc_library(
    name = "well_known_protos",
    srcs = "@submodule_protobuf//:well_known_protos",
    proto_only = True,
)
