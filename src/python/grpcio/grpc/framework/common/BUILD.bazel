load("@grpc_python_dependencies//:requirements.bzl", "requirement")
package(default_visibility = ["//visibility:public"])

py_library(
    name = "common",
    srcs = ["__init__.py",],
    deps = [
        ":cardinality",
        ":style",
    ],
)

py_library(
    name = "cardinality",
    srcs = ["cardinality.py"],
    deps = [
        requirement("enum34"),
    ],
)

py_library(
    name = "style",
    srcs = ["style.py"],
    deps = [
        requirement("enum34"),
    ],
)
