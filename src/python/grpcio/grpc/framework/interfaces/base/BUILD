load("@grpc_python_dependencies//:requirements.bzl", "requirement")
package(default_visibility = ["//visibility:public"])

py_library(
    name = "base_lib",
    srcs = ["__init__.py",],
    deps = [
        ":base",
        ":utilities",
    ],
)

py_library(
    name = "base",
    srcs = ["base.py"],
    deps = [
        "//src/python/grpcio/grpc/framework/foundation:abandonment",
        requirement("enum34"),
        requirement("six"),
    ],
)

py_library(
    name = "utilities",
    srcs = ["utilities.py"],
    deps = [
        requirement("enum34"),
    ],
)
