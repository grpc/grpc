load("@rules_java//java:defs.bzl", "java_proto_library")

load("@io_bazel_rules_go//go:def.bzl", "go_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:pgv_proto_library.bzl",
    "pgv_cc_proto_library",
    "pgv_go_proto_library",
    "pgv_java_proto_library",
    "pgv_python_proto_library",
)

proto_library(
    name = "embed_proto",
    srcs = [
        "embed.proto",
    ],
    visibility = ["//visibility:public"],
    deps = ["//validate:validate_proto"],
)

pgv_go_proto_library(
    name = "go",
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package/go",
    proto = ":embed_proto",
    deps = [
        "@com_github_golang_protobuf//ptypes:go_default_library_gen",
    ],
)

pgv_cc_proto_library(
    name = "cc",
    visibility = ["//tests:__subpackages__"],
    deps = [":embed_proto"],
)

proto_library(
    name = "other_package_proto",
    srcs = ["embed.proto"],
    visibility = ["//visibility:public"],
    deps = ["//validate:validate_proto"],
)

go_proto_library(
    name = "other_package_go_proto",
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package",
    proto = ":other_package_proto",
    visibility = ["//visibility:public"],
    deps = ["//validate:go_default_library"],
)

go_library(
    name = "go_default_library",
    embed = [":other_package_go_proto"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/other_package",
    visibility = ["//visibility:public"],
)

java_proto_library(
    name = "embed_java_proto",
    visibility = ["//visibility:public"],
    deps = [":embed_proto"],
)

pgv_java_proto_library(
    name = "java",
    java_deps = [":embed_java_proto"],
    visibility = ["//visibility:public"],
    deps = [":embed_proto"],
)

pgv_python_proto_library(
    name = "python",
    visibility = ["//visibility:public"],
    deps = [":embed_proto"],
)
