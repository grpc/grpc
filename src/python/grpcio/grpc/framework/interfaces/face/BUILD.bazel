load("@grpc_python_dependencies//:requirements.bzl", "requirement")
package(default_visibility = ["//visibility:public"])

py_library(
    name = "face",
    srcs = ["__init__.py",],
    deps = [
        ":face_lib",
        ":utilities",
    ],
)

py_library(
    name = "face_lib",
    srcs = ["face.py"],
    deps = [
        "//src/python/grpcio/grpc/framework/foundation",
        "//src/python/grpcio/grpc/framework/common",
        requirement("enum34"),
        requirement("six"),
    ],
)

py_library(
    name = "utilities",
    srcs = ["utilities.py"],
    deps = [
        "//src/python/grpcio/grpc/framework/common",
        "//src/python/grpcio/grpc/framework/foundation:stream",
        ":face_lib",
    ],
)
