load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

go_library(
    name = "go_default_library",
    srcs = [
        "cases.go",
        "executor.go",
        "harness.go",
        "worker.go",
    ],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/executor",
    visibility = ["//visibility:private"],
    deps = [
        "//tests/harness:harness_go_proto",
        "//tests/harness/cases:go",
        "//tests/harness/cases/other_package:go",
        "@com_github_golang_protobuf//proto:go_default_library",
        "@com_github_golang_protobuf//ptypes:go_default_library_gen",
        "@io_bazel_rules_go//proto/wkt:any_go_proto",
        "@io_bazel_rules_go//proto/wkt:duration_go_proto",
        "@io_bazel_rules_go//proto/wkt:timestamp_go_proto",
        "@io_bazel_rules_go//proto/wkt:wrappers_go_proto",
        "@org_golang_x_net//context:go_default_library",
    ],
)

go_binary(
    name = "executor",
    data = ["//tests/harness/cc:cc-harness"] + select({
        ":windows_x86_64": [
            "//tests/harness/go/main:go-harness-exe",
        ],
        "//conditions:default": [
            "//tests/harness/go/main:go-harness-bin",
            "//tests/harness/java:java-harness",
            "//tests/harness/python:python-harness",
        ],
    }),
    embed = [":go_default_library"],
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/executor",
    visibility = ["//visibility:private"],
)
