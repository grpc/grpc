load("//third_party/py:python_configure.bzl", "python_configure")
load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories")
load("@grpc_python_dependencies//:requirements.bzl", "pip_install")
load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_repositories")

def grpc_python_deps():
    # TODO(https://github.com/grpc/grpc/issues/18256): Remove conditional.
    if hasattr(native, "http_archive"):
        python_configure(name = "local_config_python")
        pip_repositories()
        pip_install()
        py_proto_repositories()
    else:
        print("Building python gRPC with bazel 23.0+ is disabled pending resolution of https://github.com/grpc/grpc/issues/18256.")

