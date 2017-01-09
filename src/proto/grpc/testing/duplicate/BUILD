
package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_build_system.bzl", "grpc_proto_library")

grpc_proto_library(
    name = "echo_duplicate_proto",
    srcs = ["echo_duplicate.proto"],
    deps = ["//src/proto/grpc/testing:echo_messages_proto"],
)
