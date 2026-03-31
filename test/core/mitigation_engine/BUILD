load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

grpc_package(
    name = "test/core/mitigation_engine",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_cc_test(
    name = "mitigation_engine_provider_test",
    srcs = ["mitigation_engine_provider_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings:string_view",
    ],
    deps = [
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:metadata_batch",
        "//src/core:mitigation_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)
