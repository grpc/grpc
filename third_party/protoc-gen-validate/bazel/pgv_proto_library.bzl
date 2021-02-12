load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")
load(":protobuf.bzl", "cc_proto_gen_validate", "java_proto_gen_validate", "python_proto_gen_validate")

def pgv_go_proto_library(name, proto = None, deps = [], **kwargs):
    go_proto_compiler(
        name = "pgv_plugin_go",
        suffix = ".pb.validate.go",
        valid_archive = False,
        plugin = "//:protoc-gen-validate",
        options = ["lang=go"],
    )

    go_proto_library(
        name = name,
        proto = proto,
        deps = ["//validate:go_default_library"] + deps,
        compilers = ["@io_bazel_rules_go//proto:go_proto", "pgv_plugin_go"],
        visibility = ["//visibility:public"],
        **kwargs
    )

def pgv_cc_proto_library(
        name,
        deps = [],
        cc_deps = [],
        copts = [],
        **kargs):
    """Bazel rule to create a C++ protobuf validation library from proto source files
    Args:
      name: the name of the pgv_cc_proto_library.
      deps: proto_library rules that contains the necessary .proto files.
      cc_deps: C++ dependencies of the protos being compiled. Likely cc_proto_library or pgv_cc_proto_library
      **kargs: other keyword arguments that are passed to cc_library.
    """

    native.cc_proto_library(
        name = name + "_cc_proto",
        deps = deps,
    )
    cc_proto_gen_validate(
        name = name + "_validate",
        deps = deps,
    )
    cc_library(
        name = name,
        hdrs = [":" + name + "_validate"],
        srcs = [":" + name + "_validate"],
        deps = cc_deps + [
            ":" + name + "_cc_proto",
            "@com_envoyproxy_protoc_gen_validate//validate:cc_validate",
            "@com_envoyproxy_protoc_gen_validate//validate:validate_cc",
            "@com_google_protobuf//:protobuf",
            "@com_googlesource_code_re2//:re2",
        ],
        copts = copts + select({
            "@com_envoyproxy_protoc_gen_validate//bazel:windows_x86_64": ["-DWIN32"],
            "//conditions:default": [],
        }),
        alwayslink = 1,
        **kargs
    )

def pgv_python_proto_library(
        name,
        deps = [],
        python_deps = [],
        **kwargs):
    """Bazel rule to create a Python protobuf validation library from proto sources files.
     Args:
      name: the name of the pgv_python_proto_library
      deps: proto_library rules that contain the necessary .proto files
      python_deps: Python dependencies of the protos being compiled. Likely py_proto_library or pgv_python_proto_library.
      **kwargs: other keyword arguments that are passed to the py_library.
    """

    python_proto_gen_validate(
        name = name + "_validate",
        deps = deps,
    )
    py_library(
        name = name,
        srcs = [name + "_validate"],
        deps = python_deps + [
            "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        ],
        **kwargs
    )

pgv_java_proto_library = java_proto_gen_validate
