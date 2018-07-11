load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "mv_reflection_proto",
    srcs = [
        "//src/proto/grpc/reflection/v1alpha:reflection_proto_file",
    ],
    outs = ["reflection.proto",],
    cmd = "cp $< $@",
)

py_proto_library(
    name = "py_reflection_proto",
    protos = [":mv_reflection_proto",],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
)

py_library(
    name = "grpc_reflection",
    srcs = ["reflection.py",],
    deps = [
        ":py_reflection_proto",
        "//src/python/grpcio/grpc:grpcio",
        requirement('protobuf'),
    ],
    imports=["../../",],
)


