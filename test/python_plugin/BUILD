# Build file for Python plugin tests

load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "simple_service_proto",
    srcs = ["simple_service.proto"],
)

# Test target for the standalone plugin
sh_test(
    name = "test_standalone_plugin",
    srcs = ["test_standalone_plugin.sh"],
    data = [
        "simple_service.proto",
        "//src/compiler:grpc_python_plugin",
    ],
    deps = ["@bazel_tools//tools/bash/runfiles"],
)