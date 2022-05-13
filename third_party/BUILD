load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

exports_files([
    "gtest.BUILD",
    "zlib.BUILD",
    "twisted.BUILD",
    "yaml.BUILD",
    "incremental.BUILD",
    "zope_interface.BUILD",
    "constantly.BUILD",
    "cython.BUILD",
    "six.BUILD",
    "enum34.BUILD",
    "futures.BUILD",
    "libuv.BUILD",
    "protobuf.patch",
    "rules_python.patch",
    "protoc-gen-validate.patch",
])

grpc_proto_library(
    name = "xds_proto",
    srcs = ["@envoy_api//:all_protos"],
)
