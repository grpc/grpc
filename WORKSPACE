workspace(name = "com_github_grpc_grpc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "build_stack_rules_proto",
    remote = "https://github.com/stackb/rules_proto",
    commit = "e783457abea020e7df6b94acb54f668c0473ae31",
)

git_repository(
    name = "com_google_protobuf",
    remote = "https://github.com/protocolbuffers/protobuf",
    commit = "05e81cb8f41db6e124ae8bcd52cc9abff530a31a",
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

git_repository(
    name = "io_bazel_rules_python",
    remote = "https://github.com/bazelbuild/rules_python.git",
    commit = "88532b624f74ab17138fb638d3c62750b5af5f9a",
)

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
load("@build_stack_rules_proto//cpp:deps.bzl", "cpp_proto_compile")
load("@build_stack_rules_proto//python:deps.bzl",
     "python_proto_compile", "python_proto_library",
     "python_grpc_compile", "python_grpc_library")

grpc_deps()
grpc_test_only_deps()
cpp_proto_compile()
python_proto_compile()
python_proto_library()
python_grpc_compile()
python_grpc_library()

load("//third_party/py:python_configure.bzl", "python_configure")
load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories", "pip_import")

python_configure(name = "local_config_python")
pip_repositories()

pip_import(
    name = "grpc_py_deps",
    requirements = "@build_stack_rules_proto//python:requirements.txt",
)

pip_import(
    name = "grpc_python_dependencies",
    requirements = "//:requirements.bazel.txt",
)

pip_import(
    name = "protobuf_py_deps",
    requirements = "@build_stack_rules_proto//python/requirements:protobuf.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "pip_install")
load("@protobuf_py_deps//:requirements.bzl", protobuf_pip_install = "pip_install")
load("@grpc_py_deps//:requirements.bzl", grpc_pip_install = "pip_install")

pip_install()
protobuf_pip_install()
grpc_pip_install()

register_execution_platforms(
    "//third_party/toolchains:rbe_ubuntu1604",
    "//third_party/toolchains:rbe_ubuntu1604_large",
)

register_toolchains(
    "//third_party/toolchains:cc-toolchain-clang-x86_64-default",
)
