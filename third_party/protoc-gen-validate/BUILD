load("@bazel_gazelle//:def.bzl", "gazelle")
load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")

# gazelle:prefix github.com/envoyproxy/protoc-gen-validate
# gazelle:exclude tests
gazelle(name = "gazelle")

go_binary(
    name = "protoc-gen-validate",
    embed = [":go_default_library"],
    importpath = "github.com/envoyproxy/protoc-gen-validate",
    visibility = ["//visibility:public"],
)

go_library(
    name = "go_default_library",
    srcs = ["main.go"],
    importpath = "github.com/envoyproxy/protoc-gen-validate",
    visibility = ["//visibility:private"],
    deps = [
        "//module:go_default_library",
        "@com_github_lyft_protoc_gen_star//:go_default_library",
        "@com_github_lyft_protoc_gen_star//lang/go:go_default_library",
    ],
)
