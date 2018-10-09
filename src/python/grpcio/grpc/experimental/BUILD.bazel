load("@grpc_python_dependencies//:requirements.bzl", "requirement")
package(default_visibility = ["//visibility:public"])

py_library(
    name = "experimental",
    srcs = ["__init__.py",],
    deps = [
        ":gevent",
        ":session_cache",
    ],
)

py_library(
    name = "gevent",
    srcs = ["gevent.py"],
    deps = [
        "//src/python/grpcio/grpc/_cython:cygrpc",
    ],
)

py_library(
    name = "session_cache",
    srcs = ["session_cache.py"],
    deps = [
        "//src/python/grpcio/grpc/_cython:cygrpc",
    ],
)
