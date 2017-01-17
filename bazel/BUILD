package(default_visibility = ["//:__subpackages__"])

load(":cc_grpc_library.bzl", "cc_grpc_library")

proto_library(
    name = "well_known_protos_list",
    srcs = ["@submodule_protobuf//:well_known_protos"],
)

cc_grpc_library(
    name = "well_known_protos",
    srcs = "well_known_protos_list",
    deps = [],
    proto_only = True,
)
