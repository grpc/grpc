load("//third_party/py:python_configure.bzl", "python_configure")
load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories")
load("@grpc_python_dependencies//:requirements.bzl", "pip_install")

def grpc_python_deps():
    python_configure(name = "local_config_python")
    pip_repositories()
    pip_install()
