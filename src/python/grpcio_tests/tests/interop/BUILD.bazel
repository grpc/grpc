load("@grpc_python_dependencies//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "_intraop_test_case",
    srcs = ["_intraop_test_case.py"],
    deps = [
        ":methods",
    ],
    imports=["../../",],
)

py_library(
    name = "client",
    srcs = ["client.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        ":methods",
        ":resources",
        "//src/proto/grpc/testing:py_test_proto",
        requirement('google-auth'),
    ],
    imports=["../../",],
)

py_library(
    name = "methods",
    srcs = ["methods.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        "//src/proto/grpc/testing:py_empty_proto",
        "//src/proto/grpc/testing:py_messages_proto",
        "//src/proto/grpc/testing:py_test_proto",
        requirement('google-auth'),
        requirement('enum34'),
    ],
    imports=["../../",],
)

py_library(
    name = "resources",
    srcs = ["resources.py"],
    data = [
        "//src/python/grpcio_tests/tests/interop/credentials",
    ],
)

py_library(
    name = "server",
    srcs = ["server.py"],
    deps = [
        "//src/python/grpcio/grpc:grpcio",
        ":methods",
        ":resources",
        "//src/python/grpcio_tests/tests/unit:test_common",
        "//src/proto/grpc/testing:py_test_proto",
    ],
    imports=["../../",],
)

py_test(
    name="_insecure_intraop_test",
    size="small",
    timeout="moderate",
    srcs=["_insecure_intraop_test.py",],
    main="_insecure_intraop_test.py",
    deps=[
        "//src/python/grpcio/grpc:grpcio",
        ":_intraop_test_case",
        ":methods",
        ":server",
        "//src/python/grpcio_tests/tests/unit:test_common",
        "//src/proto/grpc/testing:py_test_proto",
    ],
    imports=["../../",],
    data=[
        "//src/python/grpcio_tests/tests/unit/credentials",
    ],
)

py_test(
    name="_secure_intraop_test",
    size="small",
    timeout="moderate",
    srcs=["_secure_intraop_test.py",],
    main="_secure_intraop_test.py",
    deps=[
        "//src/python/grpcio/grpc:grpcio",
        ":_intraop_test_case",
        ":methods",
        ":server",
        "//src/python/grpcio_tests/tests/unit:test_common",
        "//src/proto/grpc/testing:py_test_proto",
    ],
    imports=["../../",],
)

