load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

py_proto_library(
    name = "test_service",
    protos = [
        "test_service.proto",
    ],
    inputs = [
        "//src/python/grpcio_tests/tests/protoc_plugin/protos/requests/r:mv_test_requests_proto",
        "//src/python/grpcio_tests/tests/protoc_plugin/protos/responses:mv_test_responses_proto",
    ],
    deps = [
        requirement('protobuf'),
    ],
)

