load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library", "py_grpc_library")

package(default_testonly = 1)

proto_library(
    name = "helloworld_proto",
    srcs = ["helloworld.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

py_proto_library(
    name = "helloworld_py_pb2",
    srcs = [":helloworld_proto"],
)

py_grpc_library(
    name = "helloworld_py_pb2_grpc",
    srcs = [":helloworld_proto"],
    deps = [":helloworld_py_pb2"],
)

py_proto_library(
    name = "duration_py_pb2",
    srcs = ["@com_google_protobuf//:duration_proto"],
)

py_proto_library(
    name = "timestamp_py_pb2",
    srcs = ["@com_google_protobuf//:timestamp_proto"],
)

py_test(
    name = "import_test",
    main = "helloworld.py",
    srcs = ["helloworld.py"],
    deps = [
        ":helloworld_py_pb2",
        ":helloworld_py_pb2_grpc",
        ":duration_py_pb2",
        ":timestamp_py_pb2",
    ],
    python_version = "PY3",
)
