load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "mv_health_proto",
    srcs = [
        "//src/proto/grpc/health/v1:health_proto_file",
    ],
    outs = ["health.proto",],
    cmd = "cp $< $@",
)

py_proto_library(
    name = "py_health_proto",
    protos = [":mv_health_proto",],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
)

py_library(
    name = "grpc_health",
    srcs = ["health.py",],
    deps = [
        ":py_health_proto",
        "//src/python/grpcio/grpc:grpcio",
    ],
    imports=["../../",],
)

