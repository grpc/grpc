load("@grpc_python_dependencies//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_test(
    name = "sanity_test",
    srcs = ["_sanity_test.py"],
    main = "_sanity_test.py",
    size = "small",
    deps = [
        "//src/python/grpcio_tests/tests",
        requirement('six'),
    ],
    imports = ["../../",],
    data = ["//src/python/grpcio_tests/tests:tests_json"],
)

