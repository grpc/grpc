load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/transport/binder")

grpc_cc_test(
    name = "binder_smoke_test",
    srcs = ["binder_smoke_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)


