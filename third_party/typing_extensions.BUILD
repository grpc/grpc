load("@rules_python//python:defs.bzl", "py_library")

genrule(
    name = "copy_typing_extensions_to_init",
    srcs = ["src/typing_extensions.py"],
    outs = ["typing_extensions/__init__.py"],
    cmd = "mkdir -p $(@D) && cp $< $(@)",
)

py_library(
    name = "typing_extensions",
    srcs = ["typing_extensions/__init__.py"],
    imports = ["."],
    visibility = ["//visibility:public"],
)
