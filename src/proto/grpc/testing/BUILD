
package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_build_system.bzl", "grpc_proto_library")

grpc_proto_library(
    name = "compiler_test_proto",
    srcs = ["compiler_test.proto"],
)

grpc_proto_library(
    name = "control_proto",
    srcs = ["control.proto"],
    deps = ["payloads_proto", "stats_proto"],
)

grpc_proto_library(
    name = "echo_messages_proto",
    srcs = ["echo_messages.proto"],
)

grpc_proto_library(
    name = "echo_proto",
    srcs = ["echo.proto"],
    deps = ["echo_messages_proto"],
)

grpc_proto_library(
    name = "empty_proto",
    srcs = ["empty.proto"],
)

grpc_proto_library(
    name = "messages_proto",
    srcs = ["messages.proto"],
)

grpc_proto_library(
    name = "metrics_proto",
    srcs = ["metrics.proto"],
)

grpc_proto_library(
    name = "payloads_proto",
    srcs = ["payloads.proto"],
)

grpc_proto_library(
    name = "services_proto",
    srcs = ["services.proto"],
    deps = ["control_proto", "messages_proto"],
)

grpc_proto_library(
    name = "stats_proto",
    srcs = ["stats.proto"],
)

grpc_proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    deps = ["empty_proto", "messages_proto"],
)
