workspace(name = "com_github_grpc_grpc")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

grpc_deps()

grpc_test_only_deps()

register_execution_platforms(
    "//third_party/toolchains:local",
    "//third_party/toolchains:local_large",
    "//third_party/toolchains:rbe_windows",
)

register_toolchains(
    "//third_party/toolchains/bazel_0.23.2_rbe_windows:cc-toolchain-x64_windows",
)

git_repository(
    name = "io_bazel_rules_python",
    commit = "fdbb17a4118a1728d19e638a5291b4c4266ea5b8",
    remote = "https://github.com/bazelbuild/rules_python.git",
)

load("@io_bazel_rules_python//python:pip.bzl", "pip_repositories", "pip_import")

pip_import(
    name = "grpc_python_dependencies",
    requirements = "//:requirements.bazel.txt",
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

load("//bazel:grpc_python_deps.bzl", "grpc_python_deps")

grpc_python_deps()

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

# Create toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_default",
)

load("@bazel_toolchains//rules:environments.bzl", "clang_env")
load("@bazel_skylib//lib:dicts.bzl", "dicts")

# Create msan toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_msan",
    env = dicts.add(
        clang_env(),
        {
            "BAZEL_LINKOPTS": "-lc++:-lc++abi:-lm",
        },
    ),
)

http_archive(
    name = "io_bazel_rules_go",
    sha256 = "45409e6c4f748baa9e05f8f6ab6efaa05739aa064e3ab94e5a1a09849c51806a",
    urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.18.7/rules_go-0.18.7.tar.gz"],
)

load("@envoy_api//bazel:repositories.bzl", "api_dependencies")

api_dependencies()

load("@io_bazel_rules_go//go/private:repositories.bzl", "go_rules_dependencies")

go_rules_dependencies()

load("@io_bazel_rules_go//go:deps.bzl", "go_rules_dependencies", "go_register_toolchains")

go_rules_dependencies()

go_register_toolchains()

load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies")

gazelle_dependencies()
