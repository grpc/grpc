package(default_visibility = ["//visibility:public"])

load("//bazel:grpc_python.bzl", "pyx_library")

pyx_library(
    name = "_protoc_compiler",
    srcs = ["_protoc_compiler.pyx"],
    deps = [
       "//:grpc",
    ],
)

