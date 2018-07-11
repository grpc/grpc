load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

py_proto_library(
    name = "requests",
    # srcs = ["requests.proto",],
    # proto_source_root = "src/python/grpcio_tests/tests/testing/proto",
    protos = ["requests.proto"],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
)

py_proto_library(
    name = "services",
    # srcs = ["requests.proto",],
    # deps = [
    #     ":requests",
    # ],
    # proto_source_root = "src/python/grpcio_tests/tests/testing/proto",
    protos = ["services.proto"],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
    proto_deps = [
        ":requests",
    ],
)

