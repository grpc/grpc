workspace(name = "com_github_grpc_grpc")

load("//bazel:grpc_deps.bzl", "grpc_deps", "grpc_test_only_deps")

grpc_deps()

grpc_test_only_deps()

load("//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

register_execution_platforms(
    "//third_party/toolchains:rbe_windows",
)

register_toolchains(
    "//third_party/toolchains/bazel_0.26.0_rbe_windows:cc-toolchain-x64_windows",
)

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "create_exec_properties_dict", "custom_exec_properties", "merge_dicts")
load("//third_party/toolchains:backwards_compatibility.bzl", "bc_exec_properties")

custom_exec_properties(
    name = "grpc_custom_exec_properties",
    constants = {
        "LARGE_MACHINE": bc_exec_properties(
            [{}, {"os": "ubuntu", "machine_size": "large"}],
        ),
    },
)

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

# Create toolchain configuration for remote execution.
rbe_autoconfig(
    name = "rbe_default",
    exec_properties = bc_exec_properties(
        [
            {
              "docker_add_capabilities": "SYS_PTRACE",
              "docker_privileged": True,
              "os_family": "Linux",
            },
            {
              "os": "ubuntu",
              "machine_size": "small",
            },
        ],
    ),
    use_legacy_platform_definition = False,
)

load("@bazel_toolchains//rules:environments.bzl", "clang_env")
load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "build_bazel_rules_android",
    sha256 = "cd06d15dd8bb59926e4d65f9003bfc20f9da4b2519985c27e190cddc8b7a7806",
    strip_prefix = "rules_android-0.1.1",
    urls = ["https://github.com/bazelbuild/rules_android/archive/v0.1.1.zip"],
)

android_sdk_repository(
    name = "androidsdk",
    # version 31.0.0 won't work https://stackoverflow.com/a/68036845
    build_tools_version = "30.0.3",
)

android_ndk_repository(
    name = "androidndk",
    # Note that Bazel does not support NDK 22 yet, and Bazel 3.7.1 only
    # supports up to API level 29 for NDK 21
    # https://github.com/bazelbuild/bazel/issues/13421
)

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

load("@io_bazel_rules_python//python:pip.bzl", "pip_import", "pip_repositories")

pip_import(
    name = "grpc_python_dependencies",
    requirements = "@com_github_grpc_grpc//:requirements.bazel.txt",
)

load("@grpc_python_dependencies//:requirements.bzl", "pip_install")

pip_repositories()

pip_install()
