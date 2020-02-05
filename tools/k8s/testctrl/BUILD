load("@io_bazel_rules_go//go:def.bzl", "go_binary")

go_binary(
    name = "testctrl",
    srcs = ["main.go"],
    deps = [
        "//src/proto/grpc/testing:testctrl_go_proto"
    ]
)

