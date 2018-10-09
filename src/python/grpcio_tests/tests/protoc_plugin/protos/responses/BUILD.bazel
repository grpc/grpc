load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "mv_test_responses_proto",
    srcs = ["test_responses.proto",],
    outs = ["tests/protoc_plugin/protos/payload/test_responses.proto",],
    cmd = "mkdir -p tests/protoc_plugin/protos/payload && cp $< $@",
)

py_proto_library(
    name = "test_responses",
    protos = [
        "test_responses.proto",
    ],
    inputs = [
        "//src/python/grpcio_tests/tests/protoc_plugin/protos/payload:mv_test_payload_proto",
    ],
    deps = [
        requirement('protobuf'),
    ],
)

