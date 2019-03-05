load("@grpc_python_dependencies//:requirements.bzl", "requirement")

py_binary(
    name = "client",
    testonly = 1,
    srcs = ["client.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio"
    ],
)

py_binary(
    name = "server",
    testonly = 1,
    srcs = ["server.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio"
    ],
)

py_test(
    name = "_multiprocessing_example_test",
    srcs = ["test/_multiprocessing_example_test.py"],
    data = [
        ":client",
        ":server"
    ]
)
