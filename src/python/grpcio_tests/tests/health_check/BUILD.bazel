package(default_visibility = ["//visibility:public"])

py_test(
    name = "health_servicer_test",
    srcs = ["_health_servicer_test.py"],
    main = "_health_servicer_test.py",
    size = "small",
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        "//src/python/grpcio_health_checking/grpc_health/v1:grpc_health",
        "//src/python/grpcio_tests/tests/unit:test_common",
    ],
    imports = ["../../",],
)


