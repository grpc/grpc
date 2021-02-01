load("//bazel:python_rules.bzl", "py2and3_test")
load("@grpc_python_dependencies//:requirements.bzl", "requirement")

def py_grpc_gevent_test(
        name,
        main = None,
        **kwargs):
    if not main:
        modified_main = name + ".py"
        if modified_main not in kwargs["srcs"]:
            fail("No file '{}' was included in srcs.".format(modified_main))
    else:
        modified_main = main
    lib_name = name + ".gevent.lib"
    native.py_library(
        name = lib_name,
        **kwargs
    )
    native.py_test(
        name = name + ".gevent.test",
        args = [name],
        deps = [
            ":{}".format(lib_name),
            requirement("gevent"),
            "//src/python/grpcio/grpc:grpcio",

            # TODO: Take in a py_test instead.
            ":resources",
        ],
        srcs = ["//bazel:_gevent_test_main.py"],
        main = "//bazel:_gevent_test_main.py",
        python_version = "PY3",
    )
