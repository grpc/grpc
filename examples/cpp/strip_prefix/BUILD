load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")

proto_library(
    name = "service_proto",
    srcs = [":service.proto"],
    import_prefix = "custom/prefix",
    strip_import_prefix = "",
)

cc_proto_library(
    name = "service_cc_proto",
    deps = [":service_proto"],
)

cc_grpc_library(
    name = "service_cc_grpc",
    srcs = [":service_proto"],
    grpc_only = True,
    deps = [":service_cc_proto"],
)
