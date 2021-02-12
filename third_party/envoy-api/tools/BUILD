load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "tap2pcap",
    srcs = ["tap2pcap.py"],
    licenses = ["notice"],  # Apache 2
    visibility = ["//visibility:public"],
    deps = ["//envoy/data/tap/v2alpha:pkg_py_proto"],
)

py_test(
    name = "tap2pcap_test",
    srcs = ["tap2pcap_test.py"],
    data = [
        "data/tap2pcap_h2_ipv4.pb_text",
        "data/tap2pcap_h2_ipv4.txt",
    ],
    # Don't run this by default, since we don't want to force local dependency on Wireshark/tshark,
    # will explicitly invoke in CI.
    tags = ["manual"],
    visibility = ["//visibility:public"],
    deps = [":tap2pcap"],
)

py_binary(
    name = "generate_listeners",
    srcs = ["generate_listeners.py"],
    licenses = ["notice"],  # Apache 2
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:pkg_py_proto",
        "//envoy/config/filter/network/http_connection_manager/v2:pkg_py_proto",
    ],
)

py_test(
    name = "generate_listeners_test",
    srcs = ["generate_listeners_test.py"],
    data = [
        "//examples/service_envoy:http_connection_manager.pb",
        "//examples/service_envoy:listeners.pb",
    ],
    visibility = ["//visibility:public"],
    deps = [":generate_listeners"],
)
