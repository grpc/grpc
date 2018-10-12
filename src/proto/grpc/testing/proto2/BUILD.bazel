load("@grpc_python_dependencies//:requirements.bzl", "requirement")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

py_proto_library(
    name = "empty2_proto",
    protos = [
        "empty2.proto",
    ],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
)

py_proto_library(
    name = "empty2_extensions_proto",
    protos = [
        "empty2_extensions.proto",
    ],
    proto_deps = [
        ":empty2_proto",
    ],
    with_grpc = True,
    deps = [
        requirement('protobuf'),
    ],
)

