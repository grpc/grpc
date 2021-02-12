load("@envoy_api//bazel:api_build_system.bzl", "api_cc_test")

licenses(["notice"])  # Apache 2

api_cc_test(
    name = "pgv_test",
    srcs = ["pgv_test.cc"],
    deps = [
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/listener:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/route:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/accesslog/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/buffer/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/fault/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/gzip/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/header_to_metadata/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/health_check/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/lua/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/router/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/squash/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/http/transcoder/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/mongo_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/health_checker/redis/v2:pkg_cc_proto",
    ],
)
