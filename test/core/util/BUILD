
load("//bazel:grpc_build_system.bzl", "grpc_test_cc_library", "grpc_test_cc_test")

grpc_test_cc_library(
    name = "gpr_test_util",
    srcs = [
        "test_config.c",
        "memory_counters.c",
    ],
    hdrs = [
        "test_config.h",
        "memory_counters.h",
    ],
    deps = ["//:gpr"],
    visibility = ["//:__subpackages__"],
)

grpc_test_cc_library(
    name = "grpc_test_util",
    srcs = [
        "grpc_profiler.c",
        "mock_endpoint.c",
        "parse_hexstring.c",
        "passthru_endpoint.c",
        "port_posix.c",
        "port_server_client.c",
        "port_windows.c",
        "reconnect_server.c",
        "slice_splitter.c",
        "test_tcp_server.c",
    ],
    hdrs = [
        "grpc_profiler.h",
        "mock_endpoint.h",
        "parse_hexstring.h",
        "passthru_endpoint.h",
        "port.h",
        "port_server_client.h",
        "reconnect_server.h",
        "slice_splitter.h",
        "test_tcp_server.h",
    ],
    deps = [":gpr_test_util", "//:grpc"],
    visibility = ["//test:__subpackages__"],
    copts = ["-std=c99"],
)

grpc_test_cc_library(
  name = "one_corpus_entry_fuzzer",
  srcs = ["one_corpus_entry_fuzzer.c"],
  deps = [":gpr_test_util", "//:grpc"],
  visibility = ["//test:__subpackages__"],
)

sh_library(
  name = "fuzzer_one_entry_runner",
  srcs = ["fuzzer_one_entry_runner.sh"],
  visibility = ["//test:__subpackages__"],
)
