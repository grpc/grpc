workspace(name = "com_github_grpc_grpc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

grpc_deps()

grpc_test_only_deps()

register_execution_platforms(
    "//third_party/toolchains:rbe_ubuntu1604",
    "//third_party/toolchains:rbe_ubuntu1604_large",
)

register_toolchains(
    "//third_party/toolchains:cc-toolchain-clang-x86_64-default",
)

http_archive(
    name = "cython",
    build_file = "//third_party:cython.BUILD",
    sha256 = "d68138a2381afbdd0876c3cb2a22389043fa01c4badede1228ee073032b07a27",
    strip_prefix = "cython-c2b80d87658a8525ce091cbe146cb7eaa29fed5c",
    urls = [
        "https://github.com/cython/cython/archive/c2b80d87658a8525ce091cbe146cb7eaa29fed5c.tar.gz",
    ],
)

load("//third_party/py:python_configure.bzl", "python_configure")

python_configure(name = "local_config_python")

git_repository(
    name = "io_bazel_rules_python",
    commit = "8b5d0683a7d878b28fffe464779c8a53659fc645",
    remote = "https://github.com/bazelbuild/rules_python.git",
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories", "pip_import")

pip_repositories()

pip_import(
    name = "grpc_python_dependencies",
    requirements = "//:requirements.bazel.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "pip_install")

pip_install()

# NOTE(https://github.com/pubref/rules_protobuf/pull/196): Switch to upstream repo after this gets merged.
git_repository(
    name = "org_pubref_rules_protobuf",
    remote = "https://github.com/ghostwriternr/rules_protobuf",
    tag = "v0.8.2.1-alpha",
)

load("@org_pubref_rules_protobuf//python:rules.bzl", "py_proto_repositories")

py_proto_repositories()
