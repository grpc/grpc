load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "mv_test_payload_proto",
    srcs = ["test_payload.proto",],
    outs = ["tests/protoc_plugin/protos/payload/test_payload.proto",],
    cmd = "mkdir -p tests/protoc_plugin/protos/payload && cp $< $@",
)

py_proto_library(
    name = "test_payload",
    protos = ["test_payload.proto",],
    deps = [
        requirement('protobuf'),
    ],
)

