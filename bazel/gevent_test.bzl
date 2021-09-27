load("@grpc_python_dependencies//:requirements.bzl", "requirement")

_GRPC_LIB = "//src/python/grpcio/grpc:grpcio"

def py_grpc_gevent_test(
        name,
        srcs,
        main = None,
        deps = None,
        data = None,
        **kwargs):
    if main == None:
        if len(srcs) != 1:
            fail("When main is not provided, srcs must be of size 1.")
        main = srcs[0]
    deps = [] if deps == None else deps
    data = [] if data == None else data
    lib_name = name + ".gevent.lib"
    supplied_python_version = kwargs.pop("python_version", "")
    if supplied_python_version and supplied_python_version != "PY3":
      fail("py_grpc_gevent_test only supports python_version=PY3")
    size = None
    if "size" in kwargs:
      size = kwargs.pop("size")
    native.py_library(
        name = lib_name,
        srcs = srcs,
        **kwargs
    )
    augmented_deps = deps + [
        ":{}".format(lib_name),
        requirement("gevent"),
    ]
    if _GRPC_LIB not in augmented_deps:
      augmented_deps.append(_GRPC_LIB)

    # The main file needs to be in the same package as the test file.
    copied_main_suffix = ".gevent.main"
    copied_main_name = name + copied_main_suffix
    copied_main_filename = copied_main_name + ".py"
    native.genrule(
        name = copied_main_name,
        srcs = ["//bazel:_gevent_test_main.py"],
        outs = [copied_main_filename],
        cmd = "cp $< $@",
    )

    native.py_test(
        name = name + ".gevent",
        args = [name],
        deps = augmented_deps,
        srcs = [copied_main_filename],
        main = copied_main_filename,
        python_version = "PY3",
        size = size,
        **kwargs,
    )
