workspace(name = "com_github_grpc_grpc")

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")

grpc_deps()

grpc_test_only_deps()

load("//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

# RBE

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")

custom_exec_properties(
    name = "grpc_custom_exec_properties",
    constants = {
        "LARGE_MACHINE": create_rbe_exec_properties_dict(
            labels = {
                "os": "ubuntu",
                "machine_size": "large",
            },
        ),
    },
)

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "platforms",
    sha256 = "8150406605389ececb6da07cbcb509d5637a3ab9a24bc69b1101531367d89d74",
    urls = ["https://github.com/bazelbuild/platforms/releases/download/0.0.8/platforms-0.0.8.tar.gz"],
)

# Prevents bazel's '...' expansion from including the following folder.
# This is required to avoid triggering "Unable to find package for @rules_fuzzing//fuzzing:cc_defs.bzl"
# error.
local_repository(
    name = "ignore_third_party_utf8_range_subtree",
    path = "third_party/utf8_range",
)

load("@rules_python//python:pip.bzl", "pip_parse")

pip_parse(
    name = "grpc_python_dependencies",
    requirements_lock = "@com_github_grpc_grpc//:requirements.bazel.lock",
)

load("@grpc_python_dependencies//:requirements.bzl", "install_deps")

install_deps()

load("@com_google_protobuf//python/dist:system_python.bzl", "system_python")

system_python(
    name = "system_python",
    minimum_python_version = "3.8",
)

load("@system_python//:pip.bzl", system_pip_parse = "pip_parse")

system_pip_parse(
    name = "pip_deps",
    requirements = "@com_google_protobuf//python:requirements.txt",
    requirements_overrides = {
        "3.11": "@com_google_protobuf//python:requirements_311.txt",
    },
)

http_archive(
    name = "build_bazel_rules_swift",
    sha256 = "bf2861de6bf75115288468f340b0c4609cc99cc1ccc7668f0f71adfd853eedb3",
    url = "https://github.com/bazelbuild/rules_swift/releases/download/1.7.1/rules_swift.1.7.1.tar.gz",
)

load(
    "@build_bazel_apple_support//lib:repositories.bzl",
    "apple_support_dependencies",
)

apple_support_dependencies()

load(
    "@build_bazel_rules_swift//swift:repositories.bzl",
    "swift_rules_dependencies",
)

swift_rules_dependencies()

http_archive(
    name = "rules_pkg",
    sha256 = "b7215c636f22c1849f1c3142c72f4b954bb12bb8dcf3cbe229ae6e69cc6479db",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_pkg/releases/download/1.1.0/rules_pkg-1.1.0.tar.gz",
        "https://github.com/bazelbuild/rules_pkg/releases/download/1.1.0/rules_pkg-1.1.0.tar.gz",
    ],
)

load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")

rules_pkg_dependencies()

# This loads the libpfm transitive dependency.
# See https://github.com/google/benchmark/pull/1520
load("@com_github_google_benchmark//:bazel/benchmark_deps.bzl", "benchmark_deps")

benchmark_deps()

# This is a transitive dependency from google_cloud_cpp
bind(
    name = "cares",
    actual = "@com_github_cares_cares//:ares",
)

load("@io_opentelemetry_cpp//bazel:repository.bzl", "opentelemetry_cpp_deps")

opentelemetry_cpp_deps()

load("@io_opentelemetry_cpp//bazel:extra_deps.bzl", "opentelemetry_extra_deps")

opentelemetry_extra_deps()

# Transitive dependency of opentelemetry_extra_deps()
bind(
    name = "madler_zlib",
    actual = "@zlib//:zlib",
)
