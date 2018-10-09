load("@grpc_python_dependencies//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_test(
    name="_reflection_servicer_test",
    size="small",
    timeout="moderate",
    srcs=["_reflection_servicer_test.py",],
    main="_reflection_servicer_test.py",
    deps=[
        "//src/python/grpcio/grpc:grpcio",
        "//src/python/grpcio_reflection/grpc_reflection/v1alpha:grpc_reflection",
        "//src/python/grpcio_tests/tests/unit:test_common",
        "//src/proto/grpc/testing:py_empty_proto",
        "//src/proto/grpc/testing/proto2:empty2_extensions_proto",
        requirement('protobuf'),
    ],
    imports=["../../",],
)

