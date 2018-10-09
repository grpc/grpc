package(default_visibility = ["//visibility:public"])

py_library(
    name = "stream_testing",
    srcs = ["stream_testing.py"],
)

py_test(
    name = "logging_pool_test",
    srcs = ["_logging_pool_test.py"],
    main = "_logging_pool_test.py",
    size = "small",
    deps = [
        "//src/python/grpcio/grpc:grpcio",
    ],
)

