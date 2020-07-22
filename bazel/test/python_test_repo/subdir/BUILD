load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "@com_github_grpc_grpc//bazel:python_rules.bzl",
    "py2and3_test",
    "py_grpc_library",
    "py_proto_library",
)

package(default_testonly = 1)

genrule(
    name = "generated_file",
    srcs = ["simple.proto"],
    outs = ["simple_copy.proto"],
    cmd = "cat $(location simple.proto) > $@"
)

proto_library(
    name = "simple_copy_proto",
    srcs = [":simple_copy.proto"],
    deps = [],
)

py_proto_library(
    name = "simple_copy_py_pb2",
    deps = [":simple_copy_proto"],
)

py2and3_test(
    name = "generated_proto_test",
    srcs = ["generated_proto_test.py"],
    main = "generated_proto_test.py",
    deps = [
        ":simple_copy_py_pb2",
    ],
)
