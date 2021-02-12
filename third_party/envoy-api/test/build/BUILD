load("@envoy_api//bazel:api_build_system.bzl", "api_cc_test", "api_go_test")

licenses(["notice"])  # Apache 2

api_cc_test(
    name = "build_test",
    srcs = ["build_test.cc"],
    deps = [
        "//envoy/api/v2:pkg_cc_proto",
        "//envoy/service/accesslog/v2:pkg_cc_proto",
        "//envoy/service/discovery/v2:pkg_cc_proto",
        "//envoy/service/metrics/v2:pkg_cc_proto",
        "//envoy/service/ratelimit/v2:pkg_cc_proto",
        "@com_github_cncf_udpa//udpa/service/orca/v1:pkg_cc_proto",
    ],
)

api_go_test(
    name = "go_build_test",
    size = "small",
    srcs = ["go_build_test.go"],
    importpath = "go_build_test",
    deps = [
        "//envoy/api/v2:pkg_go_proto",
        "//envoy/api/v2/auth:pkg_go_proto",
        "//envoy/service/accesslog/v2:pkg_go_proto",
        "//envoy/service/discovery/v2:pkg_go_proto",
        "//envoy/service/metrics/v2:pkg_go_proto",
        "//envoy/service/ratelimit/v2:pkg_go_proto",
    ],
)
