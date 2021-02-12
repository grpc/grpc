load("@rules_java//java:defs.bzl", "java_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:pgv_proto_library.bzl",
    "pgv_cc_proto_library",
    "pgv_go_proto_library",
    "pgv_java_proto_library",
    "pgv_python_proto_library",
)

# gazelle:go_generate_proto false

proto_library(
    name = "cases_proto",
    srcs = [
        "bool.proto",
        "bytes.proto",
        "enums.proto",
        "filename-with-dash.proto",
        "kitchen_sink.proto",
        "maps.proto",
        "messages.proto",
        "numbers.proto",
        "oneofs.proto",
        "repeated.proto",
        "strings.proto",
        "wkt_any.proto",
        "wkt_duration.proto",
        "wkt_timestamp.proto",
        "wkt_wrappers.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tests/harness/cases/other_package:embed_proto",
        "//validate:validate_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

pgv_go_proto_library(
    name = "go",
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go",
    proto = ":cases_proto",
    deps = [
        "//tests/harness/cases/other_package:go",
        "@com_github_golang_protobuf//ptypes:go_default_library_gen",
    ],
)

pgv_cc_proto_library(
    name = "cc",
    cc_deps = [
        "//tests/harness/cases/other_package:cc",
    ],
    visibility = ["//tests:__subpackages__"],
    deps = [":cases_proto"],
)

java_proto_library(
    name = "cases_java_proto",
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)

pgv_java_proto_library(
    name = "java",
    java_deps = [
        ":cases_java_proto",
        "//tests/harness/cases/other_package:java",
    ],
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)

pgv_python_proto_library(
    name = "python",
    python_deps = [
        "//tests/harness/cases/other_package:python",
    ],
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)
