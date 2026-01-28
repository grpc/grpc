load("//net/grpc/python:py_grpc_library.bzl", "py_grpc_library")
load("//third_party/bazel_rules/rules_python/python:proto.bzl", "py_proto_library")
load("//third_party/bazel_rules/rules_python/python:py_library.bzl", "py_library")
load("//third_party/protobuf/bazel:proto_library.bzl", "proto_library")

COMPATIBILITIES = [
    "//buildenv/target:non_prod",
]

package(
    default_applicable_licenses = ["//third_party/py/grpc_channelz:license"],
    default_visibility = ["//third_party/py/grpc_channelz:__subpackages__"],
)

# Copying is required to make the Python module importable as
# grpc_channelz.v1alpha.channelz_pb2 .
genrule(
    name = "copy_service",
    srcs = ["//src/proto/grpc/channelz/v2:service.proto"],
    outs = ["service.proto"],
    cmd = "cp $(SRCS) $(@)",
    compatible_with = COMPATIBILITIES,
)

genrule(
    name = "copy_channelz",
    srcs = ["//src/proto/grpc/channelz/v2:channelz.proto"],
    outs = ["channelz.proto"],
    cmd = "cp $(SRCS) $(@)",
    compatible_with = COMPATIBILITIES,
)

proto_library(
    name = "channelz_proto",
    srcs = ["channelz.proto"],
    compatible_with = COMPATIBILITIES,
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

py_proto_library(
    name = "channelz_py_pb2",
    compatible_with = COMPATIBILITIES,
    visibility = ["//visibility:public"],
    deps = [":channelz_proto"],
)

py_grpc_library(
    name = "channelz_py_pb2_grpc",
    srcs = [":channelz_proto"],
    compatible_with = COMPATIBILITIES,
    use_pytype = False,  # Don't want to add any additional deps.
    visibility = ["//visibility:public"],
    deps = [":channelz_py_pb2"],
)

proto_library(
    name = "service_proto",
    srcs = ["service.proto"],
    has_services = True,
    compatible_with = COMPATIBILITIES,
    visibility = ["//visibility:public"],
    deps = [
        "//src/proto/grpc/channelz/v2:channelz_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

py_proto_library(
    name = "service_py_pb2",
    compatible_with = COMPATIBILITIES,
    visibility = ["//visibility:public"],
    deps = [
        ":service_proto",
    ],
)

py_grpc_library(
    name = "service_py_pb2_grpc",
    srcs = [":service_proto"],
    compatible_with = COMPATIBILITIES,
    use_pytype = False,  # Don't want to add any additional deps.
    visibility = ["//visibility:public"],
    deps = [
        ":service_py_pb2",
    ],
)

py_library(
    name = "channelz_lib",
    srcs = glob(
        ["**/*.py"],
    ),
    compatible_with = COMPATIBILITIES,
)
