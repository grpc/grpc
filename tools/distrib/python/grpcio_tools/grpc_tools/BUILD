# TODO(rbellevi): Fix this wide-open visibility.
package(default_visibility = ["//visibility:public"])

load("//bazel:cython_library.bzl", "pyx_library")

# TODO: Move this build file up a directory to ensure that this
# points to '.', not a directory above the package root.
NON_BAZEL_ROOT = "../"

# TODO: Move BUILD file up a directory
cc_library(
    # TODO: Better name?
    name = "protoc_lib",
    srcs = ["main.cc"],
    hdrs = ["main.h"],
    deps = [
      "@com_google_protobuf//:protoc_lib",
      # I really wish there were a disaggregated target to use here.
      "//src/compiler:grpc_plugin_support",
    ],
    includes = [NON_BAZEL_ROOT],
)

pyx_library(
    name = "cyprotoc",
    srcs = ["_protoc_compiler.pyx"],
    deps = [":protoc_lib"],
    includes = [NON_BAZEL_ROOT],
)

py_library(
    name = "grpc_tools",
    srcs = ["__init__.py", "protoc.py"],
    deps = [
      ":cyprotoc",
      "@six_archive//:six",
      "@com_google_protobuf//:protobuf_python",
      "//src/python/grpcio/grpc:grpcio",
    ],
    # TODO: Think about whether we should include well-known protos.
    srcs_version = "PY2AND3",
    imports = [NON_BAZEL_ROOT],
)
