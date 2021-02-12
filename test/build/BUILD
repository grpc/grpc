load("//bazel:api_build_system.bzl", "udpa_cc_test", "udpa_go_test")

licenses(["notice"])  # Apache 2

udpa_cc_test(
    name = "build_test",
    srcs = ["build_test.cc"],
    deps = ["//udpa/service/orca/v1:pkg_cc_proto"],
)

udpa_go_test(
    name = "go_build_test",
    srcs = ["go_build_test.go"],
    importpath = "go_build_test",
    deps = [
        "//udpa/data/orca/v1:pkg_go_proto",
        "//udpa/service/orca/v1:pkg_go_proto",
        "//udpa/type/v1:pkg_go_proto",
    ],
)
