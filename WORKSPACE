workspace(name = "com_github_grpc_grpc")

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
grpc_deps()
grpc_test_only_deps()

new_http_archive(
    name = "cython",
    sha256 = "d68138a2381afbdd0876c3cb2a22389043fa01c4badede1228ee073032b07a27",
    urls = [
        "https://github.com/cython/cython/archive/c2b80d87658a8525ce091cbe146cb7eaa29fed5c.tar.gz",
    ],
    strip_prefix = "cython-c2b80d87658a8525ce091cbe146cb7eaa29fed5c",
    build_file = "//third_party:cython.BUILD",
)

load("//third_party/py:python_configure.bzl", "python_configure")
python_configure(name="local_config_python")

git_repository(
    name = "io_bazel_rules_python",
    remote = "https://github.com/bazelbuild/rules_python.git",
    commit = "8b5d0683a7d878b28fffe464779c8a53659fc645",
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories", "pip_import")

pip_repositories()
pip_import(
    name = "grpc_python_dependencies",
    requirements = "//:requirements.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "pip_install")
pip_install()

git_repository(
  name = "org_pubref_rules_protobuf",
  remote = "https://github.com/pubref/rules_protobuf",
  tag = "v0.8.2",
)

load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_repositories")
py_proto_repositories()

