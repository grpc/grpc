load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:pgv_proto_library.bzl",
    "pgv_cc_proto_library",
)

# gazelle:go_generate_proto false

proto_library(
    name = "test_proto",
    srcs = ["test.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

pgv_cc_proto_library(
    name = "test_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":test_proto"],
)

cc_binary(
    name = "cc-mfjt-test",
    srcs = ["main.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":test_cc_proto",
    ],
)
