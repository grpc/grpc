

cc_library(
    name = "gpr_test_util",
    srcs = [
        "test_config.c",
    ],
    hdrs = [
        "test_config.h",
    ],
    deps = ["//:gpr"],
    visibility = ["//:__subpackages__"],
)

cc_library(
    name = "grpc_test_util",
    srcs = [
        "grpc_profiler.c",
        "memory_counters.c",
        "mock_endpoint.c",
        "one_corpus_entry_fuzzer.c",
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
        "memory_counters.h",
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
    visibility = ["//:__subpackages__"],
)
