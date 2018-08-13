load("@grpc_python_dependencies//:requirements.bzl", "requirement")
package(default_visibility = ["//visibility:public"])

py_library(
    name = "foundation",
    srcs = ["__init__.py",],
    deps = [
        ":abandonment",
        ":callable_util",
        ":future",
        ":logging_pool",
        ":stream_util",
        ":stream",
    ],
)

py_library(
    name = "abandonment",
    srcs = ["abandonment.py"],
)

py_library(
    name = "callable_util",
    srcs = ["callable_util.py"],
    deps = [
        requirement("enum34"),
        requirement("six"),
    ],
)

py_library(
    name = "future",
    srcs = ["future.py"],
    deps = [
        requirement("six"),
    ],
)

py_library(
    name = "logging_pool",
    srcs = ["logging_pool.py"],
    deps = [
        requirement("futures"),
    ],
)

py_library(
    name = "stream_util",
    srcs = ["stream_util.py"],
    deps = [
        ":stream",
    ],
)

py_library(
    name = "stream",
    srcs = ["stream.py"],
    deps = [
        requirement("six"),
    ],
)
