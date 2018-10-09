load("@grpc_python_dependencies//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "tests_json",
    srcs = ["tests.json",],
)

py_library(
    name = "_loader",
    srcs = ["_loader.py",],
    deps = [
        requirement('coverage'),
    ],
)

py_library(
    name = "_result",
    srcs = ["_result.py",],
    deps = [
        requirement('six'),
    ],
)

py_library(
    name = "_runner",
    srcs = ["_runner.py",],
    deps = [
        ":_loader",
        ":_result",
        requirement('six'),
    ],
    imports=["../",],
)

py_library(
    name = "tests",
    srcs = ["__init__.py",],
    deps = [
        ":_loader",
        ":_runner",
    ],
    imports=["../",],
)

