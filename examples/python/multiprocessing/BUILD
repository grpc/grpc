load("@grpc_python_dependencies//:requirements.bzl", "requirement")

py_binary(
    name = "client",
    testonly = 1,
    srcs = ["client.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio"
    ],
    default_python_version = "PY3",
)

py_binary(
    name = "server",
    testonly = 1,
    srcs = ["server.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio"
    ] + select({
        "//conditions:default": [requirement("futures")],
        "//:python3": [],
    }),
    default_python_version = "PY3",
)

py_test(
    name = "test/_multiprocessing_example_test",
    srcs = ["test/_multiprocessing_example_test.py"],
    data = [
        ":client",
        ":server"
    ],
    size = "small",
)
