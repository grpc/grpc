load("@rules_python//python:defs.bzl", "py_library")

genrule(
    name = "copy_typing_extensions",
    srcs = ["src/typing_extensions.py"],
    outs = ["__init__.py"],
    cmd = "cp $< $(@)",
)

py_library(
    name = "typing_extensions",
    srcs = ["__init__.py"],
    visibility = ["//visibility:public"],
)
